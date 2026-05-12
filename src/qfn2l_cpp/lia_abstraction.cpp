#include "lia_abstraction.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <iterator>
#include <sstream>

#ifdef BACKEND_Z3
#  include "z3_factory.h"
#  include "z3_solver.h"
#endif
#ifdef BACKEND_CVC5
#  include "cvc5_factory.h"
#endif

static const char* LOG_TAG = "abs";

// Set the solver timeout. ms <= 0 means "no timeout".
// The z3 backend's set_opt() doesn't support "timeout", so we use the C++ API.
static void set_solver_timeout(const Ctx& ctx, int ms) {
#ifdef BACKEND_Z3
    if (auto* z3s = dynamic_cast<smt::Z3Solver*>(ctx.solver.get())) {
        z3::params p(*z3s->get_z3_context());
        // z3 timeout=0 means no limit; UINT_MAX is the documented default.
        p.set("timeout", ms > 0 ? (unsigned)ms : (unsigned)UINT_MAX);
        z3s->get_z3_solver()->set(p);
        return;
    }
#endif
    ctx.solver->set_opt("timeout", std::to_string(ms > 0 ? ms : 0));
}

// One-time solver initialisation for LIA options.
static void init_lia_solver(const Ctx& ctx, const Options& opts) {
    ctx.solver->set_logic("QF_NIA");  // permissive; the purified formula is LIA

#ifdef BACKEND_Z3
    // set_logic resets the z3::solver object, so seed must be set afterwards.
    if (auto* z3s = dynamic_cast<smt::Z3Solver*>(ctx.solver.get())) {
        z3::params p(*z3s->get_z3_context());
        p.set("random_seed", (unsigned)opts.seed);
        z3s->get_z3_solver()->set(p);
    }
#endif
#ifdef BACKEND_CVC5
    ctx.solver->set_opt("seed", std::to_string(opts.seed));
    ctx.solver->set_opt("produce-unsat-cores", "true");
#endif
}

// ── Purifier ──────────────────────────────────────────────────────────────────
LiaAbstraction::Purifier::Purifier(LiaAbstraction& parent)
    : TermTransformer(parent._ctx), _parent(parent), _hu(parent._ctx) {}

smt::Term LiaAbstraction::Purifier::visit_idiv(const smt::Term& t) {
    assert(is_idiv(t));
    auto x = get_child(t, 0), y = get_child(t, 1);
    if (_hu(y) || is_zero(_ctx, y)) {
        smt::Term p = _parent.make_pure_constant(t);
        if (_parent._opts.static_ax && !is_zero(_ctx, y)) {
            // |p| <= |x|  when y != 0
            smt::Term abs_p = _ctx.solver->make_term(smt::Abs, p);
            smt::Term abs_x = _ctx.solver->make_term(smt::Abs, x);
            _parent.add_axiom(p, mk_implies(_ctx,
                _ctx.solver->make_term(smt::Distinct, y, _ctx.ZERO),
                _ctx.solver->make_term(smt::Le, abs_p, abs_x)));
        }
        return p;
    }
    return t;
}

smt::Term LiaAbstraction::Purifier::visit_mod(const smt::Term& t) {
    assert(is_mod(t));
    auto y = get_child(t, 1);
    if (_hu(y) || is_zero(_ctx, y)) {
        smt::Term p = _parent.make_pure_constant(t);
        if (_parent._opts.static_ax && !is_zero(_ctx, y)) {
            smt::Term abs_y = _ctx.solver->make_term(smt::Abs, y);
            _parent.add_axiom(p, mk_implies(_ctx,
                _ctx.solver->make_term(smt::Distinct, y, _ctx.ZERO),
                mk_and2(_ctx,
                    _ctx.solver->make_term(smt::Le, _ctx.ZERO, p),
                    _ctx.solver->make_term(smt::Lt, p, abs_y))));
        }
        return p;
    }
    return t;
}

smt::Term LiaAbstraction::Purifier::visit_mul(const smt::Term& t) {
    assert(is_mul(t));
    // Flatten: expand pures-for-muls and raw nested-mul children so that
    // binary-nested x*(x*x) produces one pure for x^3 rather than two.
    auto chs = get_children(t);
    std::vector<smt::Term> to_expand(chs.begin(), chs.end());
    smt::TermVec flat;
    while (!to_expand.empty()) {
        auto c = to_expand.back(); to_expand.pop_back();
        const smt::Term* orig = _parent._pures.find_t(c);
        if (orig && is_mul(*orig)) {
            for (auto it = (*orig)->begin(); it != (*orig)->end(); ++it)
                to_expand.push_back(*it);
        } else if (is_mul(c)) {
            for (auto it = c->begin(); it != c->end(); ++it)
                to_expand.push_back(*it);
        } else {
            flat.push_back(c);
        }
    }
    std::sort(flat.begin(), flat.end(),
              [](const smt::Term& a, const smt::Term& b){
                  return a->hash() < b->hash();
              });

    smt::TermVec coeffs, others;
    for (auto& c : flat) {
        if (_hu(c)) others.push_back(c);
        else         coeffs.push_back(c);
    }
    if (others.size() <= 1) return t;

    smt::Term c = eval_mul(_ctx, coeffs);
    smt::Term o = mk_mul(_ctx, others);
    smt::Term p = _parent.make_pure_constant(o);
    return mk_mul(_ctx, {c, p});
}

smt::Term LiaAbstraction::Purifier::visit_node(const smt::Term& a) {
    smt::Term t = recurse(a);
    if (is_idiv(t)) return visit_idiv(t);
    if (is_mod(t))  return visit_mod(t);
    if (is_mul(t))  return visit_mul(t);
    return t;
}

// ── LiaAbstraction constructor ────────────────────────────────────────────────
LiaAbstraction::LiaAbstraction(const Ctx& ctx, const Options& opts,
                                const Prefix& prefix, const smt::Term& body,
                                bool is_exists)
    : _ctx(ctx)
    , _opts(opts)
    , _is_exists(is_exists)
    , _orig_vars(prefix[0].vars)
    , _hu(ctx)
    , _purify(*this)
    , _prop(ctx)
    , _prefix(prefix)
    , _body(body)
{
    init_lia_solver(_ctx, opts);
    // Run purification pass to discover all pure constants.
    smt::Term init_pure_body = _purify(_body);
    // Intermediate pures (e.g. x² created while processing x³ from binary
    // smt-switch parsing) are not reachable from the purified body.  Clear
    // their useless axioms and correct the pures counter.
    {
        CollectPures pcol(_ctx, _pures, _axioms);
        pcol(init_pure_body);
        size_t n_intermediate = _pures.term2pure().size() - pcol.collected.size();
        if (n_intermediate > 0) {
            for (auto& [t, p] : _pures.term2pure())
                if (!pcol.collected.count(p)) _axioms.erase(p);
            STATS.pures.value -= static_cast<long>(n_intermediate);
        }
        LOG(LOG_TAG, 4, "pures discovered: %zu (%zu intermediate pruned)",
            pcol.collected.size(), n_intermediate);
    }
}

// ── Logging helper ────────────────────────────────────────────────────────────
#define ALOG(lev, ...) LOG(LOG_TAG, lev, __VA_ARGS__)

// ── make_pure_constant ────────────────────────────────────────────────────────
std::string LiaAbstraction::make_fancy_name(const smt::Term& term) const {
    std::string pfx = (_is_exists ? "e_" : "u_");
    if (!is_mul(term)) return pfx;
    auto chs = get_children(term);
    if (chs.empty()) return pfx;
    // Collect distinct symbolic bases.
    smt::UnorderedTermSet seen;
    std::ostringstream oss;
    oss << pfx;
    for (auto& c : chs) {
        if (!c->is_value() && !seen.count(c)) {
            oss << c->to_string();
            seen.insert(c);
        }
    }
    return oss.str();
}

smt::Term LiaAbstraction::make_pure_constant(const smt::Term& term) {
    if (auto* p = _pures.find_p(term)) return *p;

    std::string fname = make_fancy_name(term);
    smt::Term pure = _ctx.fresh_symbol(term->get_sort(), fname);
    _pures.map_t2p(term, pure);
    STATS.pures += 1;
    ALOG(4, "mapping %s -> %s", term->to_string().c_str(), pure->to_string().c_str());
    _prefix[0].add_var(pure);

    if (is_mul(term)) {
        // Use split_mul to produce purely-linear smul axioms. Using raw children
        // would embed nonlinear subterms (e.g. (* x x)) when the term is binary-
        // nested, forcing z3 out of its LIA solver into NIA.
        MulSplit spl = split_mul(term);
        assert(is_one(_ctx, spl.coeff));

        // zero-ness axiom: pure=0 ↔ any root variable is 0
        {
            smt::TermVec zero_disjs;
            for (auto& pw : spl.pows)
                zero_disjs.push_back(
                    _ctx.solver->make_term(smt::Equal, pw[0], _ctx.ZERO));
            add_axiom(pure, _ctx.solver->make_term(smt::Equal,
                mk_or(_ctx, zero_disjs),
                _ctx.solver->make_term(smt::Equal, pure, _ctx.ZERO)), "smul");
        }

        std::vector<smt::Term> oddroots, evenroots;
        for (auto& pw : spl.pows) {
            if (pw.size() % 2 != 0) oddroots.push_back(pw[0]);
            else                    evenroots.push_back(pw[0]);
        }
        smt::Term pure_gt0 = _ctx.solver->make_term(smt::Gt, pure, _ctx.ZERO);
        if (oddroots.size() == 2) {
            assert(evenroots.empty());
            auto& r0 = oddroots[0]; auto& r1 = oddroots[1];
            smt::Term both_pos = mk_and2(_ctx,
                _ctx.solver->make_term(smt::Gt, r1, _ctx.ZERO),
                _ctx.solver->make_term(smt::Gt, r0, _ctx.ZERO));
            smt::Term both_neg = mk_and2(_ctx,
                _ctx.solver->make_term(smt::Lt, r0, _ctx.ZERO),
                _ctx.solver->make_term(smt::Lt, r1, _ctx.ZERO));
            add_axiom(pure, _ctx.solver->make_term(smt::Equal,
                mk_or2(_ctx, both_pos, both_neg), pure_gt0), "smul");
        } else if (oddroots.size() == 1) {
            smt::TermVec nonzero;
            for (auto& r : evenroots)
                nonzero.push_back(
                    _ctx.solver->make_term(smt::Distinct, r, _ctx.ZERO));
            nonzero.push_back(
                _ctx.solver->make_term(smt::Gt, oddroots[0], _ctx.ZERO));
            add_axiom(pure, _ctx.solver->make_term(smt::Equal,
                mk_and(_ctx, nonzero), pure_gt0), "smul");
        } else if (oddroots.empty()) {
            add_axiom(pure,
                _ctx.solver->make_term(smt::Ge, pure, _ctx.ZERO), "smul");
        }
    }
    return pure;
}

void LiaAbstraction::add_axiom(const smt::Term& pure, const smt::Term& ax,
                                 const char* tag) {
    ALOG(4, "ax: %s %s", ax->to_string().c_str(), tag);
    _axioms[pure].push_back(ax);
}

void LiaAbstraction::add_axioms(const smt::Term& pure, const smt::TermVec& axs,
                                  const char* tag) {
    for (auto& ax : axs) add_axiom(pure, ax, tag);
}

std::optional<smt::Term> LiaAbstraction::get_value(const smt::Term& t) const {
    if (!_lia_sat) return std::nullopt;
    try { return _ctx.solver->get_value(t); }
    catch (...) { return std::nullopt; }
}

// ── set_level ─────────────────────────────────────────────────────────────────
void LiaAbstraction::set_level(const smt::UnorderedTermMap& assignment) {
    ScopedPhase sp(STATS.set_level_time);

    _current_body = _prop(do_substitute(_ctx, _body, assignment));
    _current_pure_body = _purify(_current_body);
    assert(_current_pure_body != nullptr);

    // Only include axioms for pures reachable from the purified body (and
    // transitively from their own axioms). Intermediate pures created from
    // binary-nested parsing (e.g. x^2 when the body only has x^3) are excluded
    // — their axioms add free LIA variables that hurt search convergence.
    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);

    smt::TermVec parts = {_current_pure_body};
    for (auto& [pure, axs] : _axioms) {
        if (!pcol.collected.count(pure)) continue;
        for (auto& ax : axs)
            parts.push_back(do_substitute(_ctx, ax, assignment));
    }
    _current_instantiation = mk_and(_ctx, parts);

    ALOG(3, "instantiation done");
}

// ── solve ─────────────────────────────────────────────────────────────────────
std::optional<smt::UnorderedTermMap> LiaAbstraction::solve() {
    ScopedPhase sp_solve(STATS.solve_time);
    _solve();

    if (!_lia_sat) return std::nullopt;

    try {
        // TODO: why exception at all?
        ScopedPhase sp_comp(STATS.complete_model_time);
        smt::UnorderedTermMap model;
        for (auto& c : _orig_vars) {
            smt::Term val = _ctx.solver->get_value(c);
            model[c] = val;
        }
        return model;
    } catch (...) {
        // TODO: better crash?
        return std::nullopt;
    }
}

void LiaAbstraction::_solve() {
    // Pop previous solve session if still active.
    if (_solver_pushed) {
        _ctx.solver->pop(1);
        _solver_pushed = 0;
        _lia_sat = false;
    }

    _ctx.solver->push();
    _solver_pushed = 1;
    _ctx.solver->assert_formula(_current_instantiation);
    ALOG(4, "SAT? checking LIA");

    if (_opts.timeout > 0) {
        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
            - _opts.start_time;
        int remaining_ms = std::max(1, (int)((_opts.timeout - elapsed) * 1000));
        set_solver_timeout(_ctx, remaining_ms);
    }

    STATS.begin_phase(STATS.liatime);
    smt::Result res = _ctx.solver->check_sat();
    STATS.end_phase();
    STATS.liacalls += 1;
    set_solver_timeout(_ctx, 0);

    ALOG(4, "check done");

    if (res.is_unknown()) {
        ALOG(-1, "LIA solver returned unknown");
        _ctx.solver->pop(1);
        _solver_pushed = 0;
        throw LIAFail("LIA solver returned unknown");
    }
    if (res.is_unsat()) {
        _ctx.solver->pop(1);
        _solver_pushed = 0;
        _lia_sat = false;
        return;
    }
    _lia_sat = true;

    if (!(_opts.bounds || _opts.zeros)) return;

    // Collect pures for heuristics.
    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);
    smt::UnorderedTermSet cur_pures(pcol.collected.begin(), pcol.collected.end());
    if (cur_pures.empty()) return;

    _heuristic_left_unsat = false;
    smt::TermVec zero_assumptions;
    if (_opts.zeros) apply_zeros_heuristic(cur_pures, zero_assumptions);

    // If zeros heuristic left the solver in UNSAT state (all assumptions
    // failed), restore the model before running bounds — otherwise
    // get_value() throws in apply_bounds_heuristic and bounds is silently
    // skipped.  Python preserves current_model through zeros failure, so
    // bounds always has a valid model; we mirror that here.
    if (_heuristic_left_unsat) {
        STATS.begin_phase(STATS.liatime);
        _ctx.solver->check_sat();
        STATS.end_phase();
        STATS.liacalls += 1;
        _heuristic_left_unsat = false;
    }

    if (_opts.bounds) apply_bounds_heuristic(cur_pures, zero_assumptions);

    // Bounds heuristic may have left the solver in UNSAT state.
    if (_heuristic_left_unsat) {
        STATS.begin_phase(STATS.liatime);
        _ctx.solver->check_sat();
        STATS.end_phase();
        STATS.liacalls += 1;
    }
}

std::optional<smt::UnorderedTermMap>
LiaAbstraction::incorporate_assumptions(smt::TermVec& assumptions,
                                         const char* msg) {
    while (!assumptions.empty()) {
        ALOG(3, "incorporating %s assumptions (%zu)", msg, assumptions.size());
        set_solver_timeout(_ctx, _opts.heur_to);

        STATS.begin_phase(STATS.liatime);
        smt::Result res = _ctx.solver->check_sat_assuming(
            smt::TermVec(assumptions.begin(), assumptions.end()));
        STATS.end_phase();
        STATS.liacalls += 1;

        set_solver_timeout(_ctx, 0);

        if (res.is_unknown()) {
            ALOG(2, "%s assumptions yielded unknown", msg);
            _heuristic_left_unsat = true;
            return std::nullopt;
        }
        if (res.is_sat()) {
            ALOG(2, "successful %s assumptions", msg);
            _heuristic_left_unsat = false;
            smt::UnorderedTermMap m;
            for (auto& p : assumptions) m[p] = _ctx.solver->get_value(p);
            return m;
        }
        // UNSAT: remove conflicting assumptions via unsat assumptions.
        smt::UnorderedTermSet core;
        _ctx.solver->get_unsat_assumptions(core);
        for (auto& p : core) {
            auto it = std::find(assumptions.begin(), assumptions.end(), p);
            if (it != assumptions.end()) assumptions.erase(it);
        }
    }
    _heuristic_left_unsat = true;
    return std::nullopt;
}

void LiaAbstraction::apply_zeros_heuristic(
    const smt::UnorderedTermSet& cur_pures, smt::TermVec& assumptions) {
    for (auto& p : cur_pures)
        if (is_mul(_pures.get_t(p)))
            assumptions.push_back(
                _ctx.solver->make_term(smt::Equal, p, _ctx.ZERO));
    incorporate_assumptions(assumptions, "zeros");
}

void LiaAbstraction::apply_bounds_heuristic(
    const smt::UnorderedTermSet& cur_pures,
    const smt::TermVec& zero_assumptions) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        int64_t mx = 0;
        for (auto& p : cur_pures) {
            try {
                int64_t v = std::abs(term_to_int64(_ctx.solver->get_value(p)));
                if (v > mx) mx = v;
            } catch (...) {}
        }
        if (mx < 20) return;

        int64_t lb_v = -(3 * mx / 4), ub_v = 3 * mx / 4;
        smt::Term lb = _ctx.make_int(lb_v), ub = _ctx.make_int(ub_v);
        ALOG(3, "bounds attempt %d: [%lld, %lld]",
             attempt, (long long)lb_v, (long long)ub_v);

        smt::TermVec bounds;
        for (auto& p : cur_pures) {
            bounds.push_back(_ctx.solver->make_term(smt::Lt, lb, p));
            bounds.push_back(_ctx.solver->make_term(smt::Lt, p, ub));
        }
        smt::TermVec combined = zero_assumptions;
        combined.insert(combined.end(), bounds.begin(), bounds.end());

        set_solver_timeout(_ctx, _opts.heur_to);
        STATS.begin_phase(STATS.liatime);
        smt::Result res = _ctx.solver->check_sat_assuming(combined);
        STATS.end_phase();
        STATS.liacalls += 1;
        set_solver_timeout(_ctx, 0);

        if (!res.is_sat()) { _heuristic_left_unsat = true; return; }
        ALOG(4, "bounds attempt %d succeeded", attempt);
    }
}

// ── split_mul ─────────────────────────────────────────────────────────────────
LiaAbstraction::MulSplit LiaAbstraction::split_mul(const smt::Term& t) const {
    assert(is_mul(t));
    // Flatten nested muls so that x*(x*x) is recognised as x^3, not x*(x^2).
    smt::TermVec coeffs;
    std::unordered_map<smt::Term, smt::TermVec> pows;
    std::vector<smt::Term> stk;
    for (auto it = t->begin(); it != t->end(); ++it) stk.push_back(*it);
    while (!stk.empty()) {
        auto c = stk.back(); stk.pop_back();
        if (is_mul(c)) {
            for (auto it = c->begin(); it != c->end(); ++it) stk.push_back(*it);
        } else if (_hu(c)) {
            // Look through pures-for-muls to recover original factors.
            const smt::Term* orig = const_cast<Pures&>(_pures).find_t(c);
            if (orig && is_mul(*orig)) {
                for (auto it = (*orig)->begin(); it != (*orig)->end(); ++it)
                    stk.push_back(*it);
            } else {
                pows[c].push_back(c);
            }
        } else {
            coeffs.push_back(c);
        }
    }
    MulSplit spl;
    spl.coeff = eval_mul(_ctx, coeffs);
    for (auto& [_, v] : pows) spl.pows.push_back(v);
    assert(spl.pows.size() >= 1 && spl.pows.size() <= 2);
    return spl;
}

// ── Axiom generation ──────────────────────────────────────────────────────────
smt::TermVec LiaAbstraction::mk_pow_axioms(const smt::Term& pure,
                                             const MulSplit& split) {
    assert(split.pows.size() == 1);
    auto& pw  = split.pows[0];
    auto& root = pw[0];
    int  exp  = static_cast<int>(pw.size());

    std::optional<smt::Term> root_val_opt = get_value(root);
    if (!root_val_opt) return {};
    smt::Term root_val = *root_val_opt;

    smt::TermVec rv;
    if (is_zero(_ctx, root_val)) {
        rv.push_back(_ctx.solver->make_term(smt::Equal,
            _ctx.solver->make_term(smt::Equal, pure, _ctx.ZERO),
            _ctx.solver->make_term(smt::Equal, root, _ctx.ZERO)));
    } else {
        bool odd = (exp % 2 == 1);
        smt::Term premise  = _ctx.solver->make_term(smt::Equal, root, root_val);
        smt::Term tval     = eval_exp(_ctx, root_val, exp);
        if (odd) {
            rv.push_back(_ctx.solver->make_term(smt::Equal,
                _ctx.solver->make_term(smt::Equal, pure, tval), premise));
            smt::Term rv1  = eval_exp(_ctx,
                eval_sum(_ctx, {root_val, _ctx.ONE}), exp);
            rv.push_back(mk_or2(_ctx,
                _ctx.solver->make_term(smt::Le, pure, tval),
                _ctx.solver->make_term(smt::Ge, pure, rv1)));
        } else {
            smt::Term premise1 = _ctx.solver->make_term(smt::Equal,
                root, negate_numeral(_ctx, root_val));
            rv.push_back(_ctx.solver->make_term(smt::Equal,
                _ctx.solver->make_term(smt::Equal, pure, tval),
                mk_or2(_ctx, premise, premise1)));
            smt::Term ar = is_neg_val(root_val)
                ? negate_numeral(_ctx, root_val) : root_val;
            smt::Term ar1 = eval_sum(_ctx, {ar, _ctx.ONE});
            smt::Term tv1 = eval_exp(_ctx, ar1, exp);
            rv.push_back(mk_or2(_ctx,
                _ctx.solver->make_term(smt::Le, pure, tval),
                _ctx.solver->make_term(smt::Ge, pure, tv1)));
        }
    }
    // Linearized bounds.
    auto [clb, projlb] = lin_lb_pow(_ctx, root, exp, root_val);
    auto [cub, projub] = lin_ub_pow(_ctx, root, exp, root_val);
    rv.push_back(triple_to_axiom(_ctx, clb, projlb, pure));
    rv.push_back(triple_to_axiom(_ctx, cub, pure, projub));

    // Modular axioms.
    if (_opts.modax > 2) {
        auto pure_val_opt = get_value(pure);
        if (pure_val_opt)
            for (auto& ax : mod_ax_mul(_ctx, _opts.modax,
                    {{root, exp, root_val}}, pure, *pure_val_opt))
                rv.push_back(ax);
    }
    return rv;
}

smt::TermVec LiaAbstraction::mk_mixed_mul_axioms(const smt::Term& t,
                                                   const smt::Term& pure,
                                                   const MulSplit& split) {
    assert(split.pows.size() == 2);
    auto pw1 = split.pows[0], pw2 = split.pows[1];
    if (!get_value(pw1[0])) std::swap(pw1, pw2);
    auto& root1 = pw1[0]; auto& root2 = pw2[0];
    int exp1 = static_cast<int>(pw1.size());
    int exp2 = static_cast<int>(pw2.size());

    auto root1_val_opt = get_value(root1);
    if (!root1_val_opt) return {};
    smt::Term root1_val = *root1_val_opt;
    auto root2_val_opt  = get_value(root2);

    std::vector<std::pair<smt::Term,smt::Term>> premise_pairs;
    premise_pairs.push_back({root1, root1_val});
    if (root2_val_opt) premise_pairs.push_back({root2, *root2_val_opt});

    smt::Term cond = pairs2fla(_ctx, premise_pairs);
    smt::UnorderedTermMap subst;
    for (auto& [k,v] : premise_pairs) subst[k] = v;
    smt::Term tsubs = do_substitute(_ctx, t, subst);
    smt::Term eq_ax = mk_implies(_ctx, cond,
        _ctx.solver->make_term(smt::Equal, pure, tsubs));

    smt::TermVec rv = {eq_ax};

    if (!root2_val_opt) {
        // Only root1 is assigned — project y (root2).
        smt::Term ppow2 = _purify(mk_mul(_ctx, pw2));
        for (auto& [c, lhs, rhs] : project_y(_ctx,
                root2, exp2, root1, exp1, root1_val, ppow2, pure))
            rv.push_back(triple_to_axiom(_ctx, c, lhs, rhs));
        return rv;
    }
    smt::Term root2_val = *root2_val_opt;
    for (auto& [c, b] : combine_lb(_ctx, root1, exp1, root1_val,
                                         root2, exp2, root2_val))
        rv.push_back(triple_to_axiom(_ctx, c, b, pure));
    for (auto& [c, b] : combine_ub(_ctx, root1, exp1, root1_val,
                                         root2, exp2, root2_val))
        rv.push_back(triple_to_axiom(_ctx, c, pure, b));
    if (_opts.modax > 1) {
        auto pv = get_value(pure);
        if (pv)
            for (auto& ax : mod_ax_mul(_ctx, _opts.modax,
                    {{root1, exp1, root1_val}, {root2, exp2, root2_val}},
                    pure, *pv))
                rv.push_back(ax);
    }
    return rv;
}

smt::TermVec LiaAbstraction::mk_mul_axioms(const smt::Term& t) {
    MulSplit spl = split_mul(t);
    assert(is_one(_ctx, spl.coeff));
    assert(spl.pows.size() >= 1 && spl.pows.size() <= 2);
    smt::Term pure = _pures.get_p(t);
    return spl.pows.size() == 1
        ? mk_pow_axioms(pure, spl)
        : mk_mixed_mul_axioms(t, pure, spl);
}

smt::TermVec LiaAbstraction::mk_mod_axiom(const smt::Term& t) {
    auto x = get_child(t, 0), y = get_child(t, 1);
    smt::Term xval, yval;
    try { xval = _ctx.solver->get_value(x); } catch (...) {}
    try { yval = _ctx.solver->get_value(y); } catch (...) {}
    smt::Term pure = _pures.get_p(t);

    smt::UnorderedTermMap sx, sy;
    if (yval) sy[y] = yval;
    if (xval) sx[x] = xval;
    smt::Term tsubs_x = do_substitute(_ctx, x, sy);
    smt::Term tsubs_y = do_substitute(_ctx, y, sx);

    smt::TermVec axioms;
    if (xval && !_hu(xval)) {
        smt::Term abs_y = _ctx.solver->make_term(smt::Abs, tsubs_y);
        if (!is_neg_val(xval)) {
            axioms.push_back(mk_implies(_ctx,
                mk_and2(_ctx,
                    _ctx.solver->make_term(smt::Equal, x, xval),
                    _ctx.solver->make_term(smt::Gt, abs_y, xval)),
                _ctx.solver->make_term(smt::Equal, pure, xval)));
        } else {
            smt::Term neg_xval = negate_numeral(_ctx, xval);
            axioms.push_back(mk_implies(_ctx,
                mk_and2(_ctx,
                    _ctx.solver->make_term(smt::Equal, x, xval),
                    _ctx.solver->make_term(smt::Gt, abs_y, neg_xval)),
                _ctx.solver->make_term(smt::Equal, pure,
                    _ctx.solver->make_term(smt::Plus, xval, abs_y))));
        }
    }
    if (yval && !_hu(yval) && !is_zero(_ctx, yval)) {
        smt::Term rhs = _ctx.solver->make_term(smt::Mod, tsubs_x, yval);
        axioms.push_back(mk_implies(_ctx,
            _ctx.solver->make_term(smt::Equal, y, yval),
            _ctx.solver->make_term(smt::Equal, pure, rhs)));
    }
    return axioms;
}

smt::TermVec LiaAbstraction::mk_idiv_axiom(const smt::Term& t) {
    auto x = get_child(t, 0), y = get_child(t, 1);
    smt::Term xval, yval;
    try { xval = _ctx.solver->get_value(x); } catch (...) {}
    try { yval = _ctx.solver->get_value(y); } catch (...) {}
    smt::Term pure = _pures.get_p(t);

    smt::UnorderedTermMap sx, sy;
    if (yval) sy[y] = yval;
    if (xval) sx[x] = xval;
    smt::Term tsubs_x = do_substitute(_ctx, x, sy);
    smt::Term tsubs_y = do_substitute(_ctx, y, sx);

    smt::TermVec axioms;
    if (xval && !_hu(xval)) {
        smt::Term abs_y = _ctx.solver->make_term(smt::Abs, tsubs_y);
        if (!is_neg_val(xval)) {
            axioms.push_back(mk_implies(_ctx,
                mk_and2(_ctx,
                    _ctx.solver->make_term(smt::Equal, x, xval),
                    _ctx.solver->make_term(smt::Gt, abs_y, xval)),
                _ctx.solver->make_term(smt::Equal, pure, _ctx.ZERO)));
        } else {
            smt::Term neg_xval = negate_numeral(_ctx, xval);
            smt::Term ite = _ctx.solver->make_term(smt::Ite,
                _ctx.solver->make_term(smt::Gt, tsubs_y, _ctx.ZERO),
                _ctx.MIN_ONE, _ctx.ONE);
            axioms.push_back(mk_implies(_ctx,
                mk_and2(_ctx,
                    _ctx.solver->make_term(smt::Equal, x, xval),
                    _ctx.solver->make_term(smt::Ge, abs_y, neg_xval)),
                _ctx.solver->make_term(smt::Equal, pure, ite)));
        }
    }
    if (yval && !_hu(yval) && !is_zero(_ctx, yval)) {
        smt::Term rhs = _ctx.solver->make_term(smt::IntDiv, tsubs_x, yval);
        axioms.push_back(mk_implies(_ctx,
            _ctx.solver->make_term(smt::Equal, y, yval),
            _ctx.solver->make_term(smt::Equal, pure, rhs)));
    }
    return axioms;
}

// ── Congruence axioms ─────────────────────────────────────────────────────────
smt::TermVec LiaAbstraction::congruence_axioms_for_pair(const smt::Term& a,
                                                          const smt::Term& b) {
    const smt::Term& ta = _pures.get_t(a);
    if (is_idiv(ta) || is_mod(ta)) {
        const smt::Term& tb = _pures.get_t(b);
        auto ax  = get_child(ta, 0), ay = get_child(ta, 1);
        auto bx  = get_child(tb, 0), by = get_child(tb, 1);
        if (auto* p = _pures.find_p(ax)) ax = *p;
        if (auto* p = _pures.find_p(ay)) ay = *p;
        if (auto* p = _pures.find_p(bx)) bx = *p;
        if (auto* p = _pures.find_p(by)) by = *p;
        return {mk_implies(_ctx,
            mk_and2(_ctx,
                _ctx.solver->make_term(smt::Equal, ax, bx),
                _ctx.solver->make_term(smt::Equal, ay, by)),
            _ctx.solver->make_term(smt::Equal, a, b))};
    }
    if (is_mul(ta)) {
        MulSplit spla = split_mul(ta);
        MulSplit splb = split_mul(_pures.get_t(b));
        smt::TermVec axioms;
        if (spla.pows.size() == 2 && splb.pows.size() == 2
            && is_one(_ctx, spla.coeff) && is_one(_ctx, splb.coeff)) {
            auto [ar1,ae1] = std::make_pair(spla.pows[0][0], (int)spla.pows[0].size());
            auto [ar2,ae2] = std::make_pair(spla.pows[1][0], (int)spla.pows[1].size());
            auto [br1,be1] = std::make_pair(splb.pows[0][0], (int)splb.pows[0].size());
            auto [br2,be2] = std::make_pair(splb.pows[1][0], (int)splb.pows[1].size());
            if (ae1==be1 && ae2==be2)
                axioms.push_back(mk_implies(_ctx,
                    mk_and2(_ctx,
                        _ctx.solver->make_term(smt::Equal, ar1, br1),
                        _ctx.solver->make_term(smt::Equal, ar2, br2)),
                    _ctx.solver->make_term(smt::Equal, a, b)));
            if (ae1==be2 && ae2==be1)
                axioms.push_back(mk_implies(_ctx,
                    mk_and2(_ctx,
                        _ctx.solver->make_term(smt::Equal, ar1, br2),
                        _ctx.solver->make_term(smt::Equal, ar2, br1)),
                    _ctx.solver->make_term(smt::Equal, a, b)));
        }
        if (spla.pows.size() == 1 && splb.pows.size() == 1
            && is_one(_ctx, spla.coeff) && is_one(_ctx, splb.coeff)) {
            auto& ra = spla.pows[0][0]; int ea = static_cast<int>(spla.pows[0].size());
            auto& rb = splb.pows[0][0]; int eb = static_cast<int>(splb.pows[0].size());
            if (ea == eb) {
                smt::Term nra = _ctx.solver->make_term(smt::Negate, ra);
                smt::Term nrb = _ctx.solver->make_term(smt::Negate, rb);
                if (ea % 2 == 1) {
                    axioms.push_back(_ctx.solver->make_term(smt::Equal,
                        _ctx.solver->make_term(smt::Le, ra, rb),
                        _ctx.solver->make_term(smt::Le, a, b)));
                    axioms.push_back(_ctx.solver->make_term(smt::Equal,
                        _ctx.solver->make_term(smt::Le, rb, ra),
                        _ctx.solver->make_term(smt::Le, b, a)));
                } else {
                    // Four quadrant monotonicity axioms (even power).
                    auto make_quad = [&](smt::Term cond, smt::Term lhs_le_rhs,
                                         smt::Term a_le_b) {
                        return mk_implies(_ctx, cond,
                            _ctx.solver->make_term(smt::Equal, lhs_le_rhs, a_le_b));
                    };
                    smt::Term ra0 = _ctx.solver->make_term(smt::Ge, ra, _ctx.ZERO);
                    smt::Term rb0 = _ctx.solver->make_term(smt::Ge, rb, _ctx.ZERO);
                    smt::Term ra1 = _ctx.solver->make_term(smt::Le, ra, _ctx.ZERO);
                    smt::Term rb1 = _ctx.solver->make_term(smt::Le, rb, _ctx.ZERO);
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb0),
                        _ctx.solver->make_term(smt::Le,ra,rb),
                        _ctx.solver->make_term(smt::Le,a,b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb0),
                        _ctx.solver->make_term(smt::Le,rb,ra),
                        _ctx.solver->make_term(smt::Le,b,a)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb1),
                        _ctx.solver->make_term(smt::Le,rb,ra),
                        _ctx.solver->make_term(smt::Le,a,b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb1),
                        _ctx.solver->make_term(smt::Le,ra,rb),
                        _ctx.solver->make_term(smt::Le,b,a)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb1),
                        _ctx.solver->make_term(smt::Le,ra,nrb),
                        _ctx.solver->make_term(smt::Le,a,b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra0,rb1),
                        _ctx.solver->make_term(smt::Le,nrb,ra),
                        _ctx.solver->make_term(smt::Le,b,a)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb0),
                        _ctx.solver->make_term(smt::Le,nra,rb),
                        _ctx.solver->make_term(smt::Le,a,b)));
                    axioms.push_back(make_quad(mk_and2(_ctx,ra1,rb0),
                        _ctx.solver->make_term(smt::Le,rb,nra),
                        _ctx.solver->make_term(smt::Le,b,a)));
                }
            }
        }
        return axioms;
    }
    return {};
}

void LiaAbstraction::add_lazy_congruence_axioms(const CollectPures& pcol) {
    using It = smt::UnorderedTermSet::const_iterator;
    auto process = [&](const smt::UnorderedTermSet& collection) {
        for (auto it1 = collection.begin(); it1 != collection.end(); ++it1) {
            for (auto it2 = std::next(it1); it2 != collection.end(); ++it2) {
                int id1 = static_cast<int>((*it1)->hash() & 0x7fffffff);
                int id2 = static_cast<int>((*it2)->hash() & 0x7fffffff);
                auto key = std::make_pair(std::min(id1,id2), std::max(id1,id2));
                if (_congruence_pairs_added.count(key)) continue;
                auto candidates = congruence_axioms_for_pair(*it1, *it2);
                smt::TermVec violated_axs;
                for (auto& ax : candidates) {
                    try {
                        smt::Term v = _ctx.solver->get_value(ax);
                        if (is_false(_ctx, v)) violated_axs.push_back(ax);
                    } catch (...) {}
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
bool LiaAbstraction::is_okay(const smt::Term& pure, const smt::Term& t) {
    ALOG(4, "check_nia: %s == %s",
         pure->to_string().c_str(), t->to_string().c_str());
    smt::Term pure_val;
    try { pure_val = _ctx.solver->get_value(pure); } catch (...) { return true; }

    // Division by zero: keep the current interpretation.
    if ((is_mod(t) || is_idiv(t))) {
        smt::Term den;
        try { den = _ctx.solver->get_value(get_child(t, 1)); } catch (...) {}
        if (den && is_zero(_ctx, den)) {
            smt::Term num;
            try { num = _ctx.solver->get_value(get_child(t, 0)); } catch (...) {}
            if (is_mod(t) && num) _mod_zero_interp[num] = pure_val;
            if (is_idiv(t) && num) _idiv_zero_interp[num] = pure_val;
            return true;
        }
    }

    smt::Term tval;
    try { tval = _ctx.solver->get_value(t); } catch (...) { return true; }
    ALOG(4, "check_nia: --> %s == %s",
         pure_val->to_string().c_str(), tval->to_string().c_str());
    return pure_val == tval;
}

// ── check_nia ─────────────────────────────────────────────────────────────────
bool LiaAbstraction::check_nia() {
    assert(_lia_sat);
    ScopedPhase sp(STATS.check_nia_time);
    ALOG(3, "check_nia");

    // Quick three-valued check.
    {
        HasUninterpreted hu(_ctx);
        CheckVal cv(_ctx, hu, _pures, _ctx.solver);
        if (cv.check(_current_pure_body)) {
            ALOG(2, "check_nia quick ok");
            return true;
        }
    }

    bool res = true;
    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);

    size_t pairs_before = _congruence_pairs_added.size();
    add_lazy_congruence_axioms(pcol);
    if (_congruence_pairs_added.size() > pairs_before) res = false;

    for (auto& pure : pcol.collected) {
        const smt::Term& t = _pures.get_t(pure);
        if (is_okay(pure, t)) continue;

        res = false;
        ALOG(3, "check_nia: axioms for %s", t->to_string().c_str());
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
