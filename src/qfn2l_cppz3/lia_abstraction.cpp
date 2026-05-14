#include "lia_abstraction.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iterator>
#include <sstream>

static const char* LOG_TAG = "abs";

// Set per-call solver timeout.  ms <= 0 → no limit.
static void set_solver_timeout(z3::solver& slv, int ms) {
    z3::params p(slv.ctx());
    p.set("timeout", ms > 0 ? (unsigned)ms : (unsigned)UINT_MAX);
    slv.set(p);
}

// One-time solver setup for LIA seed.
static void init_lia_solver(const Ctx& ctx, const Options& opts) {
    z3::params p(ctx.zctx);
    p.set("random_seed", (unsigned)opts.seed);
    ctx.solver.set(p);
}

#define ALOG(lev, ...) LOG(LOG_TAG, lev, __VA_ARGS__)

// ── Purifier ──────────────────────────────────────────────────────────────────
LiaAbstraction::Purifier::Purifier(LiaAbstraction& parent)
    : TermTransformer(parent._ctx), _parent(parent), _hu(parent._ctx) {}

Term LiaAbstraction::Purifier::visit_idiv(const Term& t) {
    assert(is_idiv(t));
    auto x = t.arg(0), y = t.arg(1);
    if (_hu(y) || is_zero(_ctx, y)) {
        Term p = _parent.make_pure_constant(t);
        if (_parent._opts.static_ax && !is_zero(_ctx, y)) {
            Term abs_p = z3::abs(p);
            Term abs_x = z3::abs(x);
            _parent.add_axiom(
                p, mk_implies(_ctx, (y != _ctx.ZERO), (abs_p <= abs_x)));
        }
        return p;
    }
    return t;
}

Term LiaAbstraction::Purifier::visit_mod(const Term& t) {
    assert(is_mod(t));
    auto y = t.arg(1);
    if (_hu(y) || is_zero(_ctx, y)) {
        Term p = _parent.make_pure_constant(t);
        if (_parent._opts.static_ax && !is_zero(_ctx, y)) {
            Term abs_y = z3::abs(y);
            _parent.add_axiom(
                p, mk_implies(_ctx, (y != _ctx.ZERO),
                              mk_and2(_ctx, (_ctx.ZERO <= p), (p < abs_y))));
        }
        return p;
    }
    return t;
}

Term LiaAbstraction::Purifier::visit_mul(const Term& t) {
    assert(is_mul(t));
    // Flatten: expand pures-for-muls so that x*(x*x) → one pure for x^3.
    TermVec to_expand(t.num_args(), _ctx.ZERO);
    for (unsigned i = 0; i < t.num_args(); ++i) to_expand[i] = t.arg(i);
    TermVec flat;
    while (!to_expand.empty()) {
        auto c = to_expand.back(); to_expand.pop_back();
        const Term* orig = _parent._pures.find_t(c);
        if (orig && is_mul(*orig)) {
            for (unsigned i = 0; i < orig->num_args(); ++i)
                to_expand.push_back(orig->arg(i));
        } else if (is_mul(c)) {
            for (unsigned i = 0; i < c.num_args(); ++i)
                to_expand.push_back(c.arg(i));
        } else {
            flat.push_back(c);
        }
    }
    std::sort(flat.begin(), flat.end(),
              [](const Term& a, const Term& b) { return a.hash() < b.hash(); });

    TermVec coeffs, others;
    for (auto& c : flat) {
        if (_hu(c)) others.push_back(c);
        else        coeffs.push_back(c);
    }
    if (others.size() <= 1) return t;

    Term c = eval_mul(_ctx, coeffs);
    Term o = mk_mul(_ctx, others);
    Term p = _parent.make_pure_constant(o);
    return mk_mul(_ctx, {c, p});
}

Term LiaAbstraction::Purifier::visit_node(const Term& a) {
    Term t = recurse(a);
    if (is_idiv(t)) return visit_idiv(t);
    if (is_mod(t))  return visit_mod(t);
    if (is_mul(t))  return visit_mul(t);
    return t;
}

// ── LiaAbstraction constructor ────────────────────────────────────────────────
LiaAbstraction::LiaAbstraction(const Ctx& ctx, const Options& opts,
                               const Prefix& prefix, const Term& body,
                               bool is_exists)
    : _ctx(ctx), _opts(opts), _is_exists(is_exists),
      _orig_vars(prefix[0].vars), _hu(ctx), _purify(*this),
      _prefix(prefix), _body(body),
      _current_body(_ctx.zctx),
      _current_pure_body(_ctx.zctx),
      _current_instantiation(_ctx.zctx) {
    init_lia_solver(_ctx, opts);
    Term flat_body     = FlattenMul(_ctx)(_body);
    Term init_pure_body = _purify(flat_body);
    {
        CollectPures pcol(_ctx, _pures, _axioms);
        pcol(init_pure_body);
        size_t n_intermediate = _pures.term2pure().size() - pcol.collected.size();
        if (n_intermediate > 0) {
            for (auto& [t, p] : _pures.term2pure())
                if (!pcol.collected.count(p))
                    _axioms.erase(p);
            STATS.pures.value -= static_cast<long>(n_intermediate);
        }
        LOG(LOG_TAG, 4, "pures discovered: %zu (%zu intermediate pruned)",
            pcol.collected.size(), n_intermediate);
        _in_init = false;
        for (auto& [t, p] : _pures.term2pure()) {
            if (!pcol.collected.count(p)) continue;
            ALOG(4, "mapping %s -> %s", t.to_string().c_str(), p.to_string().c_str());
            for (auto& ax : _axioms.at(p))
                ALOG(4, "ax: %s smul", ax.to_string().c_str());
        }
    }
}

// ── make_pure_constant ────────────────────────────────────────────────────────
std::string LiaAbstraction::make_fancy_name(const Term& term) const {
    std::string pfx = (_is_exists ? "e_" : "u_");
    if (!is_mul(term)) return pfx;

    std::vector<Term> stk;
    for (unsigned i = 0; i < term.num_args(); ++i) stk.push_back(term.arg(i));
    std::vector<Term> leaves;
    while (!stk.empty()) {
        auto c = stk.back(); stk.pop_back();
        if (is_mul(c))
            for (unsigned i = 0; i < c.num_args(); ++i) stk.push_back(c.arg(i));
        else if (!c.is_numeral())
            leaves.push_back(c);
    }
    std::sort(leaves.begin(), leaves.end(),
              [](const Term& a, const Term& b) { return a.hash() < b.hash(); });

    std::ostringstream oss;
    oss << pfx;
    for (size_t i = 0; i < leaves.size();) {
        size_t j = i + 1;
        while (j < leaves.size() && leaves[j].id() == leaves[i].id()) ++j;
        int exp = static_cast<int>(j - i);
        oss << leaves[i].to_string();
        if (exp > 1) oss << "^" << exp;
        i = j;
    }
    return oss.str();
}

Term LiaAbstraction::make_pure_constant(const Term& term) {
    if (auto* p = _pures.find_p(term)) return *p;
    std::string fname = make_fancy_name(term);
    Term pure = _ctx.fresh_symbol(term.get_sort(), fname);
    _pures.map_t2p(term, pure);
    _axioms.emplace(pure, TermVec{});  // ensure key exists even before axioms are added
    STATS.pures += 1;
    if (!_in_init)
        ALOG(4, "mapping %s -> %s", term.to_string().c_str(), pure.to_string().c_str());
    _prefix[0].add_var(pure);

    if (is_mul(term)) {
        MulSplit spl = split_mul(term);
        assert(is_one(_ctx, spl.coeff));

        // zero-ness axiom: pure=0 ↔ any root variable is 0
        {
            TermVec zero_disjs;
            for (auto& pw : spl.pows)
                zero_disjs.push_back(pw[0] == _ctx.ZERO);
            add_axiom(pure,
                      (mk_or(_ctx, zero_disjs) == (pure == _ctx.ZERO)), "smul");
        }

        std::vector<Term> oddroots, evenroots;
        for (auto& pw : spl.pows) {
            if (pw.size() % 2 != 0) oddroots.push_back(pw[0]);
            else                    evenroots.push_back(pw[0]);
        }
        Term pure_gt0 = (pure > _ctx.ZERO);
        if (oddroots.size() == 2) {
            assert(evenroots.empty());
            auto& r0 = oddroots[0];
            auto& r1 = oddroots[1];
            Term both_pos = mk_and2(_ctx, (r1 > _ctx.ZERO), (r0 > _ctx.ZERO));
            Term both_neg = mk_and2(_ctx, (r0 < _ctx.ZERO), (r1 < _ctx.ZERO));
            add_axiom(pure, (mk_or2(_ctx, both_pos, both_neg) == pure_gt0), "smul");
        } else if (oddroots.size() == 1) {
            TermVec nonzero;
            for (auto& r : evenroots)
                nonzero.push_back(r != _ctx.ZERO);
            nonzero.push_back(oddroots[0] > _ctx.ZERO);
            add_axiom(pure, (mk_and(_ctx, nonzero) == pure_gt0), "smul");
        } else if (oddroots.empty()) {
            add_axiom(pure, (pure >= _ctx.ZERO), "smul");
        }
    }
    return pure;
}

void LiaAbstraction::add_axiom(const Term& pure, const Term& ax, const char* tag) {
    if (!_in_init) ALOG(4, "ax: %s %s", ax.to_string().c_str(), tag);
    _axioms[pure].push_back(ax);
}

void LiaAbstraction::add_axioms(const Term& pure, const TermVec& axs,
                                const char* tag) {
    for (auto& ax : axs) add_axiom(pure, ax, tag);
}

std::optional<Term> LiaAbstraction::get_value(const Term& t) const {
    if (!_lia_sat) return std::nullopt;
    try {
        return _ctx.get_value(t);
    } catch (...) {
        return std::nullopt;
    }
}

// ── set_level ─────────────────────────────────────────────────────────────────
void LiaAbstraction::set_level(const TermMap& assignment) {
    ScopedPhase sp(STATS.set_level_time);
    _current_body      = do_substitute(_ctx, _body, assignment);
    _current_pure_body = _purify(_current_body);
    assert(_current_pure_body.id() != 0);

    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);

    // Sort pures by descending hash to match Python's iteration order.
    std::vector<Term> ordered_pures;
    ordered_pures.reserve(_axioms.size());
    for (auto& [pure, axs] : _axioms) {
        if (!pcol.collected.count(pure)) continue;
        ordered_pures.push_back(pure);
    }
    std::sort(ordered_pures.begin(), ordered_pures.end(),
              [](const Term& a, const Term& b) { return a.hash() > b.hash(); });

    TermVec parts = {_current_pure_body};
    for (auto& pure : ordered_pures)
        for (auto& ax : _axioms.at(pure))
            parts.push_back(do_substitute(_ctx, ax, assignment));
    _current_instantiation = mk_and(_ctx, parts);

    ALOG(3, "instantiation done");
}

// ── _solve ────────────────────────────────────────────────────────────────────
void LiaAbstraction::_solve() {
    _lia_sat = false;

    // Fresh LIA solver per call — mirrors Python's SolverFor("LIA").
    z3::solver lia_slv(_ctx.zctx, "LIA");
    {
        z3::params p(_ctx.zctx);
        p.set("random_seed", (unsigned)_opts.seed);
        if (_opts.timeout > 0) {
            double elapsed =
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count() -
                _opts.start_time;
            int remaining_ms = std::max(1, (int)((_opts.timeout - elapsed) * 1000));
            p.set("timeout", (unsigned)remaining_ms);
        }
        lia_slv.set(p);
    }
    lia_slv.add(_current_instantiation);

    ALOG(5, "LIA formula:\n%s", _current_instantiation.to_string().c_str());
    ALOG(4, "SAT? checking LIA");
    STATS.begin_phase(STATS.liatime);
    z3::check_result res = lia_slv.check();
    STATS.end_phase();
    STATS.liacalls += 1;
    ALOG(4, "check done");

    if (res == z3::unknown) {
        ALOG(-1, "LIA solver returned unknown");
        throw LIAFail("LIA solver returned unknown");
    }
    if (res == z3::unsat) return;

    z3::model opt_mdl = lia_slv.get_model();

    // Adaptive model shrink (mirrors Python --bounds heuristic).
    {
        static constexpr int64_t TINY = 20;
        static constexpr int     MAX_ATTEMPTS = 5;
        for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
            int64_t mx = 0;
            for (unsigned i = 0; i < opt_mdl.num_consts(); ++i) {
                z3::expr interp =
                    opt_mdl.get_const_interp(opt_mdl.get_const_decl(i));
                if (!interp.get_sort().is_int()) continue;
                int64_t v = 0;
                if (!interp.is_numeral_i64(v)) { mx = INT64_MAX; break; }
                mx = std::max(mx, std::abs(v));
            }
            if (mx < TINY) break;
            int64_t bound = 3 * mx / 4;
            ALOG(4, "shrink attempt %d: ±%lld (max=%lld)", attempt,
                 (long long)bound, (long long)mx);
            lia_slv.push();
            for (unsigned i = 0; i < opt_mdl.num_consts(); ++i) {
                z3::func_decl d = opt_mdl.get_const_decl(i);
                if (!d.range().is_int()) continue;
                z3::expr var = d();
                lia_slv.add(var >= _ctx.zctx.int_val(-bound));
                lia_slv.add(var <= _ctx.zctx.int_val(bound));
            }
            STATS.begin_phase(STATS.liatime);
            z3::check_result bres = lia_slv.check();
            STATS.end_phase();
            STATS.liacalls += 1;
            if (bres == z3::sat) {
                ALOG(4, "shrink attempt %d succeeded", attempt);
                opt_mdl = lia_slv.get_model();
                lia_slv.pop();
            } else {
                lia_slv.pop();
                break;
            }
        }
    }

    // Replicate the model into ctx.solver so get_value() and heuristics work.
    {
        const z3::model& mdl = opt_mdl;
        _ctx.solver.reset();
        _ctx.solver.add(_current_instantiation);
        for (unsigned i = 0; i < mdl.num_consts(); ++i) {
            z3::func_decl d = mdl.get_const_decl(i);
            z3::expr interp  = mdl.get_const_interp(d);
            _ctx.solver.add(d() == interp);
        }
        _ctx.solver.check(); // trivially SAT; makes model queryable
        _lia_sat = true;
    }

    if (!(_opts.bounds || _opts.zeros)) return;

    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);
    TermSet cur_pures(pcol.collected.begin(), pcol.collected.end());
    if (cur_pures.empty()) return;

    _heuristic_left_unsat = false;
    TermVec zero_assumptions;
    if (_opts.zeros) apply_zeros_heuristic(cur_pures, zero_assumptions);

    if (_heuristic_left_unsat) {
        STATS.begin_phase(STATS.liatime);
        _ctx.solver.check();
        STATS.end_phase();
        STATS.liacalls += 1;
        _heuristic_left_unsat = false;
    }

    if (_opts.bounds) apply_bounds_heuristic(cur_pures, zero_assumptions);

    if (_heuristic_left_unsat) {
        STATS.begin_phase(STATS.liatime);
        _ctx.solver.check();
        STATS.end_phase();
        STATS.liacalls += 1;
    }
}

// ── solve ─────────────────────────────────────────────────────────────────────
std::optional<TermMap> LiaAbstraction::solve() {
    ScopedPhase sp_solve(STATS.solve_time);
    _solve();
    if (!_lia_sat) return std::nullopt;

    ScopedPhase sp_comp(STATS.complete_model_time);
    TermMap model;
    for (auto& c : _orig_vars)
        model.insert_or_assign(c, _ctx.get_value(c));
    return model;
}

// ── incorporate_assumptions ───────────────────────────────────────────────────
std::optional<TermMap>
LiaAbstraction::incorporate_assumptions(TermVec& assumptions, const char* msg) {
    while (!assumptions.empty()) {
        ALOG(3, "incorporating %s assumptions (%zu)", msg, assumptions.size());
        set_solver_timeout(_ctx.solver, _opts.heur_to);

        z3::expr_vector evec(_ctx.zctx);
        for (auto& a : assumptions) evec.push_back(a);

        STATS.begin_phase(STATS.liatime);
        z3::check_result res = _ctx.solver.check(evec);
        STATS.end_phase();
        STATS.liacalls += 1;

        set_solver_timeout(_ctx.solver, 0);

        if (res == z3::unknown) {
            ALOG(2, "%s assumptions yielded unknown", msg);
            _heuristic_left_unsat = true;
            return std::nullopt;
        }
        if (res == z3::sat) {
            ALOG(2, "successful %s assumptions", msg);
            _heuristic_left_unsat = false;
            TermMap m;
            for (auto& p : assumptions)
                m.insert_or_assign(p, _ctx.get_value(p));
            return m;
        }
        // UNSAT: remove conflicting assumptions via unsat core.
        z3::expr_vector core = _ctx.solver.unsat_core();
        for (unsigned ci = 0; ci < core.size(); ++ci) {
            auto it = std::find_if(
                assumptions.begin(), assumptions.end(),
                [&](const Term& a) { return z3::eq(a, core[ci]); });
            if (it != assumptions.end()) assumptions.erase(it);
        }
    }
    _heuristic_left_unsat = true;
    return std::nullopt;
}

void LiaAbstraction::apply_zeros_heuristic(const TermSet& cur_pures,
                                           TermVec& assumptions) {
    for (auto& p : cur_pures)
        if (is_mul(_pures.get_t(p)))
            assumptions.push_back(p == _ctx.ZERO);
    incorporate_assumptions(assumptions, "zeros");
}

void LiaAbstraction::apply_bounds_heuristic(const TermSet& cur_pures,
                                            const TermVec& zero_assumptions) {
    using boost::multiprecision::cpp_int;
    for (int attempt = 0; attempt < 5; ++attempt) {
        cpp_int mx = 0;
        for (auto& p : cur_pures) {
            auto val = get_value(p);
            if (val) {
                cpp_int v = boost::multiprecision::abs(term_to_cpp_int(*val));
                if (v > mx) mx = v;
            }
        }
        if (mx < 20) return;

        cpp_int bound     = 3 * mx / 4;
        cpp_int neg_bound = -bound;
        Term lb = cpp_int_to_term(_ctx, neg_bound);
        Term ub = cpp_int_to_term(_ctx, bound);
        ALOG(3, "bounds attempt %d: [%s, %s]", attempt,
             neg_bound.str().c_str(), bound.str().c_str());

        TermVec bounds;
        for (auto& p : cur_pures) {
            bounds.push_back(lb < p);
            bounds.push_back(p < ub);
        }
        TermVec combined = zero_assumptions;
        combined.insert(combined.end(), bounds.begin(), bounds.end());

        set_solver_timeout(_ctx.solver, _opts.heur_to);
        STATS.begin_phase(STATS.liatime);
        z3::expr_vector evec(_ctx.zctx);
        for (auto& a : combined) evec.push_back(a);
        z3::check_result res = _ctx.solver.check(evec);
        STATS.end_phase();
        STATS.liacalls += 1;
        set_solver_timeout(_ctx.solver, 0);

        if (res != z3::sat) {
            _heuristic_left_unsat = true;
            return;
        }
        ALOG(4, "bounds attempt %d succeeded", attempt);
    }
}

// ── split_mul ─────────────────────────────────────────────────────────────────
LiaAbstraction::MulSplit LiaAbstraction::split_mul(const Term& t) const {
    assert(is_mul(t));
    TermVec coeffs;
    std::unordered_map<Term, TermVec, ExprHash, ExprEq> pows;
    std::vector<Term> stk;
    for (unsigned i = 0; i < t.num_args(); ++i) stk.push_back(t.arg(i));
    while (!stk.empty()) {
        auto c = stk.back(); stk.pop_back();
        if (is_mul(c)) {
            for (unsigned i = 0; i < c.num_args(); ++i) stk.push_back(c.arg(i));
        } else if (_hu(c)) {
            const Term* orig = const_cast<Pures&>(_pures).find_t(c);
            if (orig && is_mul(*orig)) {
                for (unsigned i = 0; i < orig->num_args(); ++i)
                    stk.push_back(orig->arg(i));
            } else {
                pows[c].push_back(c);
            }
        } else {
            coeffs.push_back(c);
        }
    }
    MulSplit spl{eval_mul(_ctx, coeffs), {}};
    for (auto& [_, v] : pows) spl.pows.push_back(v);
    assert(spl.pows.size() >= 1 && spl.pows.size() <= 2);
    return spl;
}

// ── Axiom generation ──────────────────────────────────────────────────────────
TermVec LiaAbstraction::mk_pow_axioms(const Term& pure, const MulSplit& split) {
    assert(split.pows.size() == 1);
    auto& pw   = split.pows[0];
    auto& root = pw[0];
    int   exp  = static_cast<int>(pw.size());

    auto root_val_opt = get_value(root);
    if (!root_val_opt) return {};
    Term root_val = *root_val_opt;

    TermVec rv;
    if (is_zero(_ctx, root_val)) {
        rv.push_back((pure == _ctx.ZERO) == (root == _ctx.ZERO));
    } else {
        bool odd    = (exp % 2 == 1);
        Term premise = (root == root_val);
        Term tval    = eval_exp(_ctx, root_val, exp);
        if (odd) {
            rv.push_back(((pure == tval) == premise));
            Term rv1 = eval_exp(_ctx, eval_sum(_ctx, {root_val, _ctx.ONE}), exp);
            rv.push_back(mk_or2(_ctx, (pure <= tval), (pure >= rv1)));
        } else {
            Term premise1 = (root == negate_numeral(_ctx, root_val));
            rv.push_back(((pure == tval) == mk_or2(_ctx, premise, premise1)));
            Term ar  = is_neg_val(root_val) ? negate_numeral(_ctx, root_val) : root_val;
            Term ar1 = eval_sum(_ctx, {ar, _ctx.ONE});
            Term tv1 = eval_exp(_ctx, ar1, exp);
            rv.push_back(mk_or2(_ctx, (pure <= tval), (pure >= tv1)));
        }
    }
    auto [clb, projlb] = lin_lb_pow(_ctx, root, exp, root_val);
    auto [cub, projub] = lin_ub_pow(_ctx, root, exp, root_val);
    rv.push_back(triple_to_axiom(_ctx, clb, projlb, pure));
    rv.push_back(triple_to_axiom(_ctx, cub, pure, projub));

    if (_opts.modax > 2) {
        auto pv_opt = get_value(pure);
        if (pv_opt)
            for (auto& ax : mod_ax_mul(_ctx, _opts.modax,
                                       {{root, exp, root_val}}, pure, *pv_opt))
                rv.push_back(ax);
    }
    return rv;
}

TermVec LiaAbstraction::mk_mixed_mul_axioms(const Term& t, const Term& pure,
                                            const MulSplit& split) {
    assert(split.pows.size() == 2);
    auto pw1 = split.pows[0], pw2 = split.pows[1];
    if (!get_value(pw1[0])) std::swap(pw1, pw2);
    auto& root1 = pw1[0];
    auto& root2 = pw2[0];
    int   exp1  = static_cast<int>(pw1.size());
    int   exp2  = static_cast<int>(pw2.size());

    auto root1_val_opt = get_value(root1);
    if (!root1_val_opt) return {};
    Term root1_val = *root1_val_opt;
    auto root2_val_opt = get_value(root2);

    std::vector<std::pair<Term, Term>> premise_pairs;
    premise_pairs.push_back({root1, root1_val});
    if (root2_val_opt) premise_pairs.push_back({root2, *root2_val_opt});

    Term cond = pairs2fla(_ctx, premise_pairs);
    TermMap subst;
    for (auto& [k, v] : premise_pairs) subst.insert_or_assign(k, v);
    Term tsubs  = do_substitute(_ctx, t, subst);
    Term eq_ax  = mk_implies(_ctx, cond, (pure == tsubs));

    TermVec rv = {eq_ax};

    if (!root2_val_opt) {
        Term ppow2 = _purify(mk_mul(_ctx, pw2));
        for (auto& [c, lhs, rhs] :
             project_y(_ctx, root2, exp2, root1, exp1, root1_val, ppow2, pure))
            rv.push_back(triple_to_axiom(_ctx, c, lhs, rhs));
        return rv;
    }
    Term root2_val = *root2_val_opt;
    for (auto& [c, b] :
         combine_lb(_ctx, root1, exp1, root1_val, root2, exp2, root2_val))
        rv.push_back(triple_to_axiom(_ctx, c, b, pure));
    for (auto& [c, b] :
         combine_ub(_ctx, root1, exp1, root1_val, root2, exp2, root2_val))
        rv.push_back(triple_to_axiom(_ctx, c, pure, b));
    if (_opts.modax > 1) {
        auto pv = get_value(pure);
        if (pv)
            for (auto& ax :
                 mod_ax_mul(_ctx, _opts.modax,
                            {{root1, exp1, root1_val}, {root2, exp2, root2_val}},
                            pure, *pv))
                rv.push_back(ax);
    }
    return rv;
}

TermVec LiaAbstraction::mk_mul_axioms(const Term& t) {
    MulSplit spl = split_mul(t);
    assert(is_one(_ctx, spl.coeff));
    assert(spl.pows.size() >= 1 && spl.pows.size() <= 2);
    Term pure = _pures.get_p(t);
    return spl.pows.size() == 1 ? mk_pow_axioms(pure, spl)
                                : mk_mixed_mul_axioms(t, pure, spl);
}

TermVec LiaAbstraction::mk_mod_axiom(const Term& t) {
    auto x = t.arg(0), y = t.arg(1);
    Term xval = get_value(x).value_or(Term(_ctx.zctx));
    Term yval = get_value(y).value_or(Term(_ctx.zctx));
    Term pure = _pures.get_p(t);

    TermMap sx, sy;
    if (yval.id() != 0) sy.insert_or_assign(y, yval);
    if (xval.id() != 0) sx.insert_or_assign(x, xval);
    Term tsubs_x = do_substitute(_ctx, x, sy);
    Term tsubs_y = do_substitute(_ctx, y, sx);

    TermVec axioms;
    if (xval.id() != 0 && !_hu(xval)) {
        Term abs_y = z3::abs(tsubs_y);
        if (!is_neg_val(xval)) {
            axioms.push_back(
                mk_implies(_ctx,
                           mk_and2(_ctx, (x == xval), (abs_y > xval)),
                           (pure == xval)));
        } else {
            Term neg_xval = negate_numeral(_ctx, xval);
            axioms.push_back(mk_implies(
                _ctx,
                mk_and2(_ctx, (x == xval), (abs_y > neg_xval)),
                (pure == (xval + abs_y))));
        }
    }
    if (yval.id() != 0 && !_hu(yval) && !is_zero(_ctx, yval)) {
        Term rhs = z3::mod(tsubs_x, yval);
        axioms.push_back(mk_implies(_ctx, (y == yval), (pure == rhs)));
    }
    return axioms;
}

TermVec LiaAbstraction::mk_idiv_axiom(const Term& t) {
    auto x = t.arg(0), y = t.arg(1);
    Term xval = get_value(x).value_or(Term(_ctx.zctx));
    Term yval = get_value(y).value_or(Term(_ctx.zctx));
    Term pure = _pures.get_p(t);

    TermMap sx, sy;
    if (yval.id() != 0) sy.insert_or_assign(y, yval);
    if (xval.id() != 0) sx.insert_or_assign(x, xval);
    Term tsubs_x = do_substitute(_ctx, x, sy);
    Term tsubs_y = do_substitute(_ctx, y, sx);

    TermVec axioms;
    if (xval.id() != 0 && !_hu(xval)) {
        Term abs_y = z3::abs(tsubs_y);
        if (!is_neg_val(xval)) {
            axioms.push_back(
                mk_implies(_ctx,
                           mk_and2(_ctx, (x == xval), (abs_y > xval)),
                           (pure == _ctx.ZERO)));
        } else {
            Term neg_xval = negate_numeral(_ctx, xval);
            Term ite = z3::ite(tsubs_y > _ctx.ZERO, _ctx.MIN_ONE, _ctx.ONE);
            axioms.push_back(
                mk_implies(_ctx,
                           mk_and2(_ctx, (x == xval), (abs_y >= neg_xval)),
                           (pure == ite)));
        }
    }
    if (yval.id() != 0 && !_hu(yval) && !is_zero(_ctx, yval)) {
        Term rhs = tsubs_x / yval; // integer division
        axioms.push_back(mk_implies(_ctx, (y == yval), (pure == rhs)));
    }
    return axioms;
}

// ── Congruence axioms ─────────────────────────────────────────────────────────
TermVec LiaAbstraction::congruence_axioms_for_pair(const Term& a, const Term& b) {
    const Term& ta = _pures.get_t(a);
    if (is_idiv(ta) || is_mod(ta)) {
        const Term& tb = _pures.get_t(b);
        auto ax = ta.arg(0), ay = ta.arg(1);
        auto bx = tb.arg(0), by = tb.arg(1);
        if (auto* p = _pures.find_p(ax)) ax = *p;
        if (auto* p = _pures.find_p(ay)) ay = *p;
        if (auto* p = _pures.find_p(bx)) bx = *p;
        if (auto* p = _pures.find_p(by)) by = *p;
        return {mk_implies(_ctx,
                           mk_and2(_ctx, (ax == bx), (ay == by)),
                           (a == b))};
    }
    if (is_mul(ta)) {
        MulSplit spla = split_mul(ta);
        MulSplit splb = split_mul(_pures.get_t(b));
        TermVec axioms;
        if (spla.pows.size() == 2 && splb.pows.size() == 2 &&
            is_one(_ctx, spla.coeff) && is_one(_ctx, splb.coeff)) {
            auto [ar1, ae1] = std::make_pair(spla.pows[0][0], (int)spla.pows[0].size());
            auto [ar2, ae2] = std::make_pair(spla.pows[1][0], (int)spla.pows[1].size());
            auto [br1, be1] = std::make_pair(splb.pows[0][0], (int)splb.pows[0].size());
            auto [br2, be2] = std::make_pair(splb.pows[1][0], (int)splb.pows[1].size());
            if (ae1 == be1 && ae2 == be2)
                axioms.push_back(mk_implies(_ctx,
                    mk_and2(_ctx, (ar1 == br1), (ar2 == br2)), (a == b)));
            if (ae1 == be2 && ae2 == be1)
                axioms.push_back(mk_implies(_ctx,
                    mk_and2(_ctx, (ar1 == br2), (ar2 == br1)), (a == b)));
        }
        if (spla.pows.size() == 1 && splb.pows.size() == 1 &&
            is_one(_ctx, spla.coeff) && is_one(_ctx, splb.coeff)) {
            auto& ra = spla.pows[0][0];
            int   ea = static_cast<int>(spla.pows[0].size());
            auto& rb = splb.pows[0][0];
            int   eb = static_cast<int>(splb.pows[0].size());
            if (ea == eb) {
                Term nra = -ra, nrb = -rb;
                if (ea % 2 == 1) {
                    axioms.push_back(((ra <= rb) == (a <= b)));
                    axioms.push_back(((rb <= ra) == (b <= a)));
                } else {
                    auto make_quad = [&](Term cond, Term lhs_le_rhs, Term a_le_b) {
                        return mk_implies(_ctx, cond, (lhs_le_rhs == a_le_b));
                    };
                    Term ra0 = (ra >= _ctx.ZERO), rb0 = (rb >= _ctx.ZERO);
                    Term ra1 = (ra <= _ctx.ZERO), rb1 = (rb <= _ctx.ZERO);
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb0),(ra<=rb),(a<=b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb0),(rb<=ra),(b<=a)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb1),(rb<=ra),(a<=b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb1),(ra<=rb),(b<=a)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb1),(ra<=nrb),(a<=b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb1),(nrb<=ra),(b<=a)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb0),(nra<=rb),(a<=b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb0),(rb<=nra),(b<=a)));
                }
            }
        }
        return axioms;
    }
    return {};
}

void LiaAbstraction::add_lazy_congruence_axioms(const CollectPures& pcol) {
    auto process = [&](const TermSet& collection) {
        for (auto it1 = collection.begin(); it1 != collection.end(); ++it1) {
            for (auto it2 = std::next(it1); it2 != collection.end(); ++it2) {
                // canonical order by id
                Term lo = it1->id() < it2->id() ? *it1 : *it2;
                Term hi = it1->id() < it2->id() ? *it2 : *it1;
                auto key = std::make_pair(lo, hi);
                if (_congruence_pairs_added.count(key)) continue;
                auto candidates = congruence_axioms_for_pair(*it1, *it2);
                TermVec violated_axs;
                for (auto& ax : candidates) {
                    auto v = get_value(ax);
                    if (v && is_false(_ctx, *v))
                        violated_axs.push_back(ax);
                }
                if (!violated_axs.empty()) {
                    add_axioms(*it1, violated_axs, "cong");
                    _congruence_pairs_added.insert(key);
                }
            }
        }
    };
    process(pcol.idiv_collected);
    process(pcol.mod_collected);
    process(pcol.mul_collected);
}

// ── is_okay ───────────────────────────────────────────────────────────────────
bool LiaAbstraction::is_okay(const Term& pure, const Term& t) {
    ALOG(4, "check_nia: %s == %s", pure.to_string().c_str(), t.to_string().c_str());
    auto pure_val_opt = get_value(pure);
    if (!pure_val_opt) return true;
    Term pure_val = *pure_val_opt;

    if (is_mod(t) || is_idiv(t)) {
        Term den = get_value(t.arg(1)).value_or(Term(_ctx.zctx));
        if (den.id() != 0 && is_zero(_ctx, den)) {
            Term num = get_value(t.arg(0)).value_or(Term(_ctx.zctx));
            if (is_mod(t)  && num.id() != 0) _mod_zero_interp.insert_or_assign(num, pure_val);
            if (is_idiv(t) && num.id() != 0) _idiv_zero_interp.insert_or_assign(num, pure_val);
            return true;
        }
    }

    auto tval_opt = get_value(t);
    if (!tval_opt) return true;
    Term tval = *tval_opt;
    ALOG(4, "check_nia: --> %s == %s",
         pure_val.to_string().c_str(), tval.to_string().c_str());
    return z3::eq(pure_val, tval);
}

// ── check_nia ─────────────────────────────────────────────────────────────────
bool LiaAbstraction::check_nia() {
    assert(_lia_sat);
    ScopedPhase sp(STATS.check_nia_time);
    ALOG(3, "check_nia");

    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);

    if (g_verbosity >= 3) {
        for (auto& pure : pcol.collected) {
            auto pv = get_value(pure);
            ALOG(3, "  %s = %s", pure.to_string().c_str(),
                 pv ? (*pv).to_string().c_str() : "?");
        }
    }

    {
        HasUninterpreted hu(_ctx);
        CheckVal cv(_ctx, hu, _pures);
        if (cv.check(_current_pure_body)) {
            ALOG(2, "check_nia quick ok");
            return true;
        }
    }

    bool res = true;

    size_t pairs_before = _congruence_pairs_added.size();
    add_lazy_congruence_axioms(pcol);
    if (_congruence_pairs_added.size() > pairs_before) res = false;

    for (auto& pure : pcol.collected) {
        const Term& t = _pures.get_t(pure);
        if (is_okay(pure, t)) continue;

        res = false;
        ALOG(3, "check_nia: axioms for %s", t.to_string().c_str());
        if (is_idiv(t)) {
            auto axs = mk_idiv_axiom(t);
            add_axioms(pure, axs, "div");
            STATS.div_axioms += static_cast<long>(axs.size());
        } else if (is_mod(t)) {
            auto axs = mk_mod_axiom(t);
            add_axioms(pure, axs, "mod");
            STATS.mod_axioms += static_cast<long>(axs.size());
        } else if (is_mul(t)) {
            auto axs = mk_mul_axioms(t);
            add_axioms(pure, axs, "mul");
            STATS.mul_axioms += static_cast<long>(axs.size());
        }
    }
    return res;
}
