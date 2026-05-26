#include "lia_abstraction.h"
#include "projections.h"
#include "stats.h"
#include "tagged_logging.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <iterator>
#include <optional>
#include <sstream>

#ifdef BACKEND_Z3
#include "z3_solver.h"
#endif
#ifdef BACKEND_CVC5
#include "cvc5_factory.h"
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
    ctx.solver->set_logic("QF_NIA"); // permissive; the purified formula is LIA

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

#define ALOG(lev, ...) LOG(LOG_TAG, lev, __VA_ARGS__)

static boost::multiprecision::cpp_int
pow_cpp_int(boost::multiprecision::cpp_int base, const int exp) {
    assert(exp >= 0);
    boost::multiprecision::cpp_int res = 1;
    int e = exp;
    while (e > 0) {
        if (e & 1)
            res *= base;
        e >>= 1;
        if (e)
            base *= base;
    }
    return res;
}

static std::optional<boost::multiprecision::cpp_int>
exact_integer_root(const boost::multiprecision::cpp_int& value, int exp) {
    assert(exp > 0);
    if (exp == 1)
        return value;
    if (value == 0)
        return boost::multiprecision::cpp_int(0);
    if (value < 0 && exp % 2 == 0)
        return std::nullopt;

    const boost::multiprecision::cpp_int target = value < 0 ? -value : value;
    boost::multiprecision::cpp_int lo = 0, hi = 1;
    while (pow_cpp_int(hi, exp) < target)
        hi <<= 1;

    while (lo <= hi) {
        const boost::multiprecision::cpp_int mid = (lo + hi) >> 1;
        const boost::multiprecision::cpp_int mp = pow_cpp_int(mid, exp);
        if (mp == target)
            return value < 0 ? -mid : mid;
        if (mp < target)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return std::nullopt;
}

static std::string terms_to_string(const smt::TermVec& terms) {
    std::ostringstream out;
    bool first = true;
    for (const auto& t : terms) {
        if (!first)
            out << ", ";
        out << t->to_string();
        first = false;
    }
    return out.str();
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
            smt::Term abs_p = mk_int_abs(_ctx, p);
            smt::Term abs_x = mk_int_abs(_ctx, x);
            _parent.add_axiom(
                p, mk_implies(_ctx, _ctx.solver->make_term(smt::Distinct, y, _ctx.ZERO),
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
            smt::Term abs_y = mk_int_abs(_ctx, y);
            _parent.add_axiom(
                p,
                mk_implies(_ctx, _ctx.solver->make_term(smt::Distinct, y, _ctx.ZERO),
                           mk_and2(_ctx, _ctx.solver->make_term(smt::Le, _ctx.ZERO, p),
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
        auto c = to_expand.back();
        to_expand.pop_back();
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
    std::sort(flat.begin(), flat.end(), [](const smt::Term& a, const smt::Term& b) {
        return a->hash() < b->hash();
    });

    smt::TermVec coeffs, others;
    for (auto& c : flat) {
        if (_hu(c))
            others.push_back(c);
        else
            coeffs.push_back(c);
    }
    if (others.size() <= 1)
        return t;

    smt::Term c = eval_mul(_ctx, coeffs);
    smt::Term o = mk_mul(_ctx, others);
    smt::Term p = _parent.make_pure_constant(o);
    return mk_mul(_ctx, {c, p});
}

smt::Term LiaAbstraction::Purifier::visit_node(const smt::Term& a) {
    smt::Term t = recurse(a);
    if (is_idiv(t))
        return visit_idiv(t);
    if (is_mod(t))
        return visit_mod(t);
    if (is_mul(t))
        return visit_mul(t);
    return t;
}

// ── LiaAbstraction constructor ────────────────────────────────────────────────
LiaAbstraction::LiaAbstraction(const Ctx& ctx, const Options& opts,
                               const Prefix& prefix, const smt::Term& body,
                               bool is_exists)
    : _ctx(ctx), _opts(opts), _is_exists(is_exists), _orig_vars(prefix[0].vars),
      _hu(ctx), _purify(*this), _prefix(prefix), _body(body) {
    init_lia_solver(_ctx, opts);
    // Pre-flatten nested muls so z3's binary-nested (* x (* x x)) becomes the
    // flat (* x x x) before purification.  This prevents the purifier from
    // creating intermediate pures (e.g. x²) that would immediately be discarded.
    smt::Term flat_body = FlattenMul(_ctx)(_body);
    smt::Term init_pure_body = _purify(flat_body);
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
            if (!pcol.collected.count(p))
                continue;
            ALOG(4, "mapping %s -> %s", t->to_string().c_str(), p->to_string().c_str());
            if (auto ait = _axioms.find(p); ait != _axioms.end())
                for (auto& ax : ait->second)
                    ALOG(4, "ax: %s smul", ax->to_string().c_str());
        }
    }
}

// ── make_pure_constant ────────────────────────────────────────────────────────
std::string LiaAbstraction::make_fancy_name(const smt::Term& term) const {
    std::string pfx = (_is_exists ? "e_" : "u_");
    if (!is_mul(term))
        return pfx;

    // Flatten nested muls (z3 binary-nests n-ary products) and count occurrences.
    std::vector<smt::Term> stk;
    for (auto it = term->begin(); it != term->end(); ++it)
        stk.push_back(*it);
    std::vector<smt::Term> leaves;
    while (!stk.empty()) {
        auto c = stk.back();
        stk.pop_back();
        if (is_mul(c)) {
            for (auto it = c->begin(); it != c->end(); ++it)
                stk.push_back(*it);
        } else if (!c->is_value()) {
            leaves.push_back(c);
        }
    }
    std::sort(leaves.begin(), leaves.end(), [](const smt::Term& a, const smt::Term& b) {
        return a->hash() < b->hash();
    });

    std::ostringstream oss;
    oss << pfx;
    for (size_t i = 0; i < leaves.size();) {
        size_t j = i + 1;
        while (j < leaves.size() && leaves[j] == leaves[i])
            ++j;
        int exp = static_cast<int>(j - i);
        oss << leaves[i]->to_string();
        if (exp > 1)
            oss << "^" << exp;
        i = j;
    }
    return oss.str();
}

smt::Term LiaAbstraction::make_pure_constant(const smt::Term& term) {
    if (auto* p = _pures.find_p(term))
        return *p;

    std::string fname = make_fancy_name(term);
    smt::Term pure = _ctx.fresh_symbol(term->get_sort(), fname);
    _pures.map_t2p(term, pure);
    STATS.pures += 1;
    if (!_in_init)
        ALOG(4, "mapping %s -> %s", term->to_string().c_str(),
             pure->to_string().c_str());
    _prefix[0].add_var(pure);

    // Sign/zero axioms for mul pures are added lazily in check_nia (mk_sign_axioms).
    return pure;
}

void LiaAbstraction::add_axiom(const smt::Term& pure, const smt::Term& ax,
                               const char* tag) {
    if (!_in_init)
        ALOG(4, "ax: %s %s", ax->to_string().c_str(), tag);
    _axioms[pure].push_back(ax);
}

void LiaAbstraction::add_axioms(const smt::Term& pure, const smt::TermVec& axs,
                                const char* tag) {
    for (auto& ax : axs)
        add_axiom(pure, ax, tag);
}

std::optional<smt::Term> LiaAbstraction::get_value(const smt::Term& t) const {
    if (!_lia_sat)
        return std::nullopt;
    try {
        return _ctx.solver->get_value(t);
    } catch (...) {
        return std::nullopt;
    }
}

// ── set_level ─────────────────────────────────────────────────────────────────
void LiaAbstraction::set_level(const smt::UnorderedTermMap& assignment) {
    ScopedPhase sp(STATS.set_level_time);

    // Do NOT run SimplePropagate here: it would substitute MakeDefs definition
    // equalities (e.g. _p_0 = x0-9) back into the formula, undoing the binary
    // factoring that MakeDefs introduced and creating 3+-factor pures that
    // split_mul cannot handle. The definition equalities stay in _current_body
    // as plain linear LIA constraints for the solver.
    _current_body = do_substitute(_ctx, _body, assignment);
    _current_pure_body = _purify(_current_body);
    assert(_current_pure_body != nullptr);

    // Only include axioms for pures reachable from the purified body (and
    // transitively from their own axioms). Intermediate pures created from
    // binary-nested parsing (e.g. x^2 when the body only has x^3) are excluded
    // — their axioms add free LIA variables that hurt search convergence.
    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);

    // Sort pures by descending hash so pures created later (which correspond to
    // variables declared earlier in the original formula, due to z3 reversing
    // argument order internally) appear first. This matches Python's insertion-
    // order iteration and steers z3 toward the same model-selection choices.
    std::vector<smt::Term> ordered_pures;
    ordered_pures.reserve(_axioms.size());
    for (auto& [pure, axs] : _axioms) {
        if (!pcol.collected.count(pure))
            continue;
        ordered_pures.push_back(pure);
    }
    std::sort(
        ordered_pures.begin(), ordered_pures.end(),
        [](const smt::Term& a, const smt::Term& b) { return a->hash() > b->hash(); });

    smt::TermVec parts = {_current_pure_body};
    for (auto& pure : ordered_pures) {
        for (auto& ax : _axioms.at(pure))
            parts.push_back(do_substitute(_ctx, ax, assignment));
    }
    _current_instantiation = mk_and(_ctx, parts);

    ALOG(3, "instantiation done");
}

// ── solve ─────────────────────────────────────────────────────────────────────
std::optional<smt::UnorderedTermMap> LiaAbstraction::solve() {
    ScopedPhase sp_solve(STATS.solve_time);
    _solve();

    if (!_lia_sat)
        return std::nullopt;

    ScopedPhase sp_comp(STATS.complete_model_time);
    smt::UnorderedTermMap model;
    for (auto& c : _orig_vars) {
        smt::Term val = _ctx.solver->get_value(c);
        model[c] = val;
    }
    return model;
}

void LiaAbstraction::_solve() {
    _lia_sat = false;

#ifdef BACKEND_Z3
    if (auto* z3s = dynamic_cast<smt::Z3Solver*>(_ctx.solver.get())) {
        z3::context* z3ctx = z3s->get_z3_context();

        // Fresh solver per call, tuned for LIA — mirrors Python's SolverFor("LIA").
        z3::solver lia_slv(*z3ctx, "QF_LIA");
        z3::params p(*z3ctx);
        p.set("random_seed", (unsigned)_opts.seed);
        if (_opts.timeout > 0) {
            double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count() -
                             _opts.start_time;
            int remaining_ms = std::max(1, (int)((_opts.timeout - elapsed) * 1000));
            p.set("timeout", (unsigned)remaining_ms);
        }
        lia_slv.set(p);

        auto* z3t = dynamic_cast<smt::Z3Term*>(_current_instantiation.get());
        assert(z3t);

        z3::expr lia_expr = z3t->get_z3_expr();
        if (_opts.lia_preproc) {
            try {
                z3::tactic t = z3::tactic(*z3ctx, "simplify") &
                               z3::tactic(*z3ctx, "propagate-values");
                z3::goal g(*z3ctx);
                g.add(lia_expr);
                z3::apply_result res = t(g);
                if (res.size() == 1)
                    lia_expr = res[0].as_expr();
            } catch (...) {}
        }
        lia_slv.add(lia_expr);

        ALOG(5, "LIA formula:\n%s", lia_expr.to_string().c_str());

        // --bounds: try to find a small model using per-variable bounds, growing
        // bounds guided by unsat cores when the bounded problem is UNSAT.
        bool got_sat = false;
        z3::model opt_mdl(*z3ctx);

        if (_opts.bounds) {
            struct BndVar {
                z3::expr var;
                int64_t bound;
            };
            // Only bound variables that appear directly inside a nonlinear term
            // (mul/div/mod operand). Variables that are purely linear do not need
            // bounds — the LIA solver handles them without help.
            smt::UnorderedTermSet nl_vars;
            for (auto& [orig_term, _] : _pures.term2pure())
                for (auto& v : get_vars(orig_term))
                    nl_vars.insert(v);

            std::vector<BndVar> bvars;
            for (auto& sv : _orig_vars) {
                if (!nl_vars.count(sv))
                    continue;
                auto* z3tv = dynamic_cast<smt::Z3Term*>(sv.get());
                if (!z3tv)
                    continue;
                z3::expr e = z3tv->get_z3_expr();
                if (!e.get_sort().is_int())
                    continue;
                bvars.push_back({e, _opts.bounds_initial});
            }

            if (!bvars.empty()) {
                static constexpr int MAX_BND_ROUNDS = 5;
                static constexpr int64_t MAX_BND_VAL = (int64_t)1 << 40;

                for (int rnd = 0; rnd < MAX_BND_ROUNDS && !got_sat; rnd++) {
                    // Add bound assertions via named indicators so unsat_core()
                    // reliably identifies which bounds contributed to UNSAT.
                    lia_slv.push();
                    z3::expr_vector asmps(*z3ctx);
                    std::vector<std::pair<z3::expr, z3::expr>> inds; // (lo, hi) per var
                    for (int i = 0; i < (int)bvars.size(); i++) {
                        std::string nm = std::to_string(rnd) + "_" + std::to_string(i);
                        z3::expr p_lo = z3ctx->bool_const(("_blo_" + nm).c_str());
                        z3::expr p_hi = z3ctx->bool_const(("_bhi_" + nm).c_str());
                        lia_slv.add(implies(p_lo, bvars[i].var >=
                                                       z3ctx->int_val(-bvars[i].bound)));
                        lia_slv.add(implies(p_hi, bvars[i].var <=
                                                       z3ctx->int_val(bvars[i].bound)));
                        inds.push_back({p_lo, p_hi});
                        asmps.push_back(p_lo);
                        asmps.push_back(p_hi);
                    }

                    {
                        int64_t mx_bnd = 0;
                        for (auto& bv : bvars)
                            mx_bnd = std::max(mx_bnd, bv.bound);
                        ALOG(4, "bounds rnd %d: max bound +-%lld", rnd, (long long)mx_bnd);
                    }
                    STATS.begin_phase(STATS.liatime);
                    z3::check_result bres = lia_slv.check(asmps);
                    STATS.end_phase();
                    STATS.liacalls += 1;

                    if (bres == z3::sat) {
                        ALOG(4, "bounds: SAT at rnd %d", rnd);
                        opt_mdl = lia_slv.get_model();
                        got_sat = true;
                        lia_slv.pop();
                        break;
                    } else if (bres == z3::unsat) {
                        z3::expr_vector core = lia_slv.unsat_core();
                        lia_slv.pop();

                        bool any_bound_in_core = false;
                        std::vector<bool> in_core(bvars.size(), false);
                        for (unsigned j = 0; j < core.size(); j++) {
                            unsigned cid = core[j].id();
                            for (int k = 0; k < (int)bvars.size(); k++) {
                                if (cid == inds[k].first.id() ||
                                    cid == inds[k].second.id()) {
                                    in_core[k] = true;
                                    any_bound_in_core = true;
                                }
                            }
                        }
                        if (!any_bound_in_core) {
                            ALOG(4, "bounds: truly UNSAT at rnd %d", rnd);
                            return; // _lia_sat stays false
                        }
                        ALOG(4, "bounds rnd %d: UNSAT from bounds, growing", rnd);
                        for (int k = 0; k < (int)bvars.size(); k++) {
                            if (in_core[k] && bvars[k].bound < MAX_BND_VAL)
                                bvars[k].bound =
                                    std::max(bvars[k].bound + 1, bvars[k].bound * 3 / 2);
                        }
                    } else {
                        lia_slv.pop();
                        ALOG(4, "bounds rnd %d: unknown, giving up bounds", rnd);
                        break;
                    }
                }
            }
        }

        if (!got_sat) {
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
            if (res == z3::unsat)
                return;

            opt_mdl = lia_slv.get_model();
        }

        // Adaptively shrink the model: mirror Python's --bounds heuristic.
        // Each attempt bounds all int constants to ±(3/4 * current_max) and
        // re-checks.  Stop when max < 20 or the bounded check is UNSAT.
        {
            static constexpr int64_t TINY = 20;
            static constexpr int MAX_ATTEMPTS = 5;
            for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
                int64_t mx = 0;
                for (unsigned i = 0; i < opt_mdl.num_consts(); i++) {
                    z3::expr interp =
                        opt_mdl.get_const_interp(opt_mdl.get_const_decl(i));
                    if (!interp.get_sort().is_int())
                        continue;
                    int64_t v = 0;
                    if (!interp.is_numeral_i64(v)) {
                        mx = INT64_MAX;
                        break;
                    }
                    // Guard against std::abs(INT64_MIN) UB.
                    mx = std::max(mx, v == INT64_MIN ? INT64_MAX : std::abs(v));
                }
                if (mx == INT64_MAX || mx < TINY)
                    break;
                int64_t bound = 3 * mx / 4;
                ALOG(4, "shrink attempt %d: +-%lld (max=%lld)", attempt,
                     (long long)bound, (long long)mx);
                lia_slv.push();
                for (unsigned i = 0; i < opt_mdl.num_consts(); i++) {
                    z3::func_decl d = opt_mdl.get_const_decl(i);
                    if (!d.range().is_int())
                        continue;
                    z3::expr var = d();
                    lia_slv.add(var >= z3ctx->int_val(-bound));
                    lia_slv.add(var <= z3ctx->int_val(bound));
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

        // SAT: replicate the model into _ctx.solver so that get_value() and
        // heuristics (which call check_sat_assuming on _ctx.solver) work normally.
        const z3::model& mdl = opt_mdl;
        z3::solver& slv = *z3s->get_z3_solver();
        slv.reset();
        slv.add(z3t->get_z3_expr());
        for (unsigned i = 0; i < mdl.num_consts(); i++) {
            z3::func_decl d = mdl.get_const_decl(i);
            z3::expr interp = mdl.get_const_interp(d);
            slv.add(d() == interp);
        }
        slv.check(); // trivially SAT; makes the model available via get_value()
        _lia_sat = true;

        if (!_opts.zeros)
            return;

        CollectPures pcol(_ctx, _pures, _axioms);
        pcol(_current_pure_body);
        smt::UnorderedTermSet cur_pures(pcol.collected.begin(), pcol.collected.end());
        if (cur_pures.empty())
            return;

        _heuristic_left_unsat = false;
        smt::TermVec zero_assumptions;
        if (_opts.zeros)
            apply_zeros_heuristic(cur_pures, zero_assumptions);

        if (_heuristic_left_unsat) {
            STATS.begin_phase(STATS.liatime);
            _ctx.solver->check_sat();
            STATS.end_phase();
            STATS.liacalls += 1;
            _heuristic_left_unsat = false;
        }

        return;
    }
#endif

    // Non-z3 fallback: reset before each call so axioms don't accumulate.
    _ctx.solver->reset();
    _ctx.solver->assert_formula(_current_instantiation);
    ALOG(4, "SAT? checking LIA");

    if (_opts.timeout > 0) {
        double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count() -
                         _opts.start_time;
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
        throw LIAFail("LIA solver returned unknown");
    }
    if (res.is_unsat())
        return;
    _lia_sat = true;

    if (!_opts.zeros)
        return;

    CollectPures pcol(_ctx, _pures, _axioms);
    pcol(_current_pure_body);
    smt::UnorderedTermSet cur_pures(pcol.collected.begin(), pcol.collected.end());
    if (cur_pures.empty())
        return;

    _heuristic_left_unsat = false;
    smt::TermVec zero_assumptions;
    if (_opts.zeros)
        apply_zeros_heuristic(cur_pures, zero_assumptions);

    if (_heuristic_left_unsat) {
        STATS.begin_phase(STATS.liatime);
        _ctx.solver->check_sat();
        STATS.end_phase();
        STATS.liacalls += 1;
        _heuristic_left_unsat = false;
    }
}

std::optional<smt::UnorderedTermMap>
LiaAbstraction::incorporate_assumptions(smt::TermVec& assumptions, const char* msg) {
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
            for (auto& p : assumptions)
                m[p] = _ctx.solver->get_value(p);
            return m;
        }
        // UNSAT: remove conflicting assumptions via unsat assumptions.
        smt::UnorderedTermSet core;
        _ctx.solver->get_unsat_assumptions(core);
        for (auto& p : core) {
            auto it = std::find(assumptions.begin(), assumptions.end(), p);
            if (it != assumptions.end())
                assumptions.erase(it);
        }
    }
    _heuristic_left_unsat = true;
    return std::nullopt;
}

void LiaAbstraction::apply_zeros_heuristic(const smt::UnorderedTermSet& cur_pures,
                                           smt::TermVec& assumptions) {
    for (auto& p : cur_pures)
        if (is_mul(_pures.get_t(p)))
            assumptions.push_back(_ctx.solver->make_term(smt::Equal, p, _ctx.ZERO));
    incorporate_assumptions(assumptions, "zeros");
}

bool LiaAbstraction::apply_model_fix_sub(const CheckVal::ModelFixInfo& info, int max_iters) {
    if (info.wrong_pures.empty() || info.relevant_vars.empty())
        return false;

    // Build pinning equalities for original vars outside the wrong-pure neighborhood.
    smt::TermVec pinned_smt;
    for (const auto& var : _orig_vars) {
        if (info.relevant_vars.count(var))
            continue;
        const auto val = get_value(var);
        if (!val)
            continue;
        pinned_smt.push_back(_ctx.solver->make_term(smt::Equal, var, *val));
    }

    if (pinned_smt.empty()) {
        ALOG(5, "model_fix_sub: all %zu vars are relevant, skipping",
             _orig_vars.size());
        return false;
    }

    STATS.model_fix_attempts += 1;
    ALOG(3, "model_fix_sub: relevant=%zu pinning=%zu/%zu",
         info.relevant_vars.size(), pinned_smt.size(), _orig_vars.size());

#ifdef BACKEND_Z3
    if (auto* z3s = dynamic_cast<smt::Z3Solver*>(_ctx.solver.get())) {
        z3::context* z3ctx = z3s->get_z3_context();
        z3::solver& main_slv = *z3s->get_z3_solver();

        auto* z3t = dynamic_cast<smt::Z3Term*>(_current_instantiation.get());
        if (!z3t)
            return false;
        const z3::expr lia_expr = z3t->get_z3_expr();

        // Save current model so we can restore it if the sub-iteration fails.
        const z3::model saved_mdl = main_slv.get_model();

        // Replicate a z3 model into main_slv (same pattern as _solve()).
        auto replicate_to_main = [&](const z3::model& mdl) -> z3::check_result {
            main_slv.reset();
            main_slv.add(lia_expr);
            for (unsigned i = 0; i < mdl.num_consts(); i++) {
                z3::func_decl d = mdl.get_const_decl(i);
                main_slv.add(d() == mdl.get_const_interp(d));
            }
            return main_slv.check();
        };

        // Fresh restricted solver: formula + pinned irrelevant vars.
        z3::solver sub_slv(*z3ctx, "QF_LIA");
        {
            z3::params p(*z3ctx);
            p.set("random_seed", (unsigned)_opts.seed);
            p.set("timeout", (unsigned)_opts.heur_to);
            sub_slv.set(p);
        }
        sub_slv.add(lia_expr);
        for (const auto& eq : pinned_smt) {
            auto* z3eq = dynamic_cast<smt::Z3Term*>(eq.get());
            if (z3eq)
                sub_slv.add(z3eq->get_z3_expr());
        }

        for (int sub_it = 0; max_iters < 0 || sub_it < max_iters; sub_it++) {
            STATS.begin_phase(STATS.liatime);
            const z3::check_result res = sub_slv.check();
            STATS.end_phase();
            STATS.liacalls += 1;

            if (res != z3::sat) {
                ALOG(5, "model_fix_sub: sub_it %d %s", sub_it,
                     res == z3::unsat ? "unsat" : "unknown");
                break;
            }

            replicate_to_main(sub_slv.get_model());

            HasUninterpreted hu(_ctx);
            CheckVal cv(_ctx, hu, _pures, _ctx.solver);
            if (cv.check(_current_pure_body)) {
                STATS.model_fix_successes += 1;
                ALOG(2, "model_fix_sub: repaired at sub-iter %d", sub_it);
                return true;
            }

            // NIA still wrong — generate axioms for wrong pures and feed them
            // to sub_slv so the next sub-iteration is more constrained.
            CollectPures pcol(_ctx, _pures, _axioms);
            pcol(_current_pure_body);
            bool any_new = false;
            for (const auto& pure : pcol.collected) {
                const smt::Term& t = _pures.get_t(pure);
                if (is_okay(pure, t))
                    continue;
                any_new = true;
                ALOG(5, "model_fix_sub: sub_it %d axioms for %s", sub_it,
                     t->to_string().c_str());
                smt::TermVec axs;
                if (is_idiv(t)) {
                    axs = mk_idiv_axiom(t);
                    add_axioms(pure, axs, "div");
                    STATS.div_axioms += static_cast<long>(axs.size());
                } else if (is_mod(t)) {
                    axs = mk_mod_axiom(t);
                    add_axioms(pure, axs, "mod");
                    STATS.mod_axioms += static_cast<long>(axs.size());
                } else if (is_mul(t)) {
                    if (!_sign_axioms_added.count(pure)) {
                        MulSplit spl = split_mul(t);
                        const auto sax = mk_sign_axioms(pure, spl);
                        add_axioms(pure, sax, "smul");
                        for (const auto& ax : sax) {
                            auto* z3ax = dynamic_cast<smt::Z3Term*>(ax.get());
                            if (z3ax)
                                sub_slv.add(z3ax->get_z3_expr());
                        }
                        _sign_axioms_added.insert(pure);
                    }
                    axs = mk_mul_axioms(t);
                    add_axioms(pure, axs, "mul");
                    STATS.mul_axioms += static_cast<long>(axs.size());
                }
                for (const auto& ax : axs) {
                    auto* z3ax = dynamic_cast<smt::Z3Term*>(ax.get());
                    if (z3ax)
                        sub_slv.add(z3ax->get_z3_expr());
                }
            }
            if (!any_new) {
                ALOG(5, "model_fix_sub: sub_it %d NIA wrong but no new axioms", sub_it);
                break;
            }
        }

        // Sub-iteration failed — restore the original model.
        if (replicate_to_main(saved_mdl) != z3::sat) {
            // saved_mdl no longer satisfies lia_expr (axioms added during sub-iterations
            // tightened it). Fall back: solve lia_expr alone to recover any valid model.
            main_slv.reset();
            main_slv.add(lia_expr);
            main_slv.check();
        }
        return false;
    }
#endif

    return false;
}

bool LiaAbstraction::apply_model_fix(const CheckVal::ModelFixInfo& info) {
    auto restore_model = [&] {
        STATS.begin_phase(STATS.liatime);
        _ctx.solver->check_sat();
        STATS.end_phase();
        STATS.liacalls += 1;
    };

    auto try_fix = [&](const char* technique, const smt::Term& pure,
                       const smt::Term& t, const smt::TermVec& assumptions) {
        if (assumptions.empty())
            return false;

        ALOG(3, "model_fix: trying %zu assignments", assumptions.size());
        ALOG(5, "model_fix: technique=%s pure=%s term=%s", technique,
             pure->to_string().c_str(), t->to_string().c_str());
        if (g_verbosity >= 5)
            ALOG(5, "model_fix: assumptions=%s", terms_to_string(assumptions).c_str());
        STATS.model_fix_attempts += 1;
        set_solver_timeout(_ctx, _opts.heur_to);
        STATS.begin_phase(STATS.liatime);
        const smt::Result res = _ctx.solver->check_sat_assuming(assumptions);
        STATS.end_phase();
        STATS.liacalls += 1;
        set_solver_timeout(_ctx, 0);

        ALOG(5, "model_fix: check_sat_assuming -> %s",
             res.is_sat() ? "sat" : (res.is_unsat() ? "unsat" : "unknown"));
        if (!res.is_sat()) {
            restore_model();
            return false;
        }

        if (g_verbosity >= 5) {
            for (const auto& a : assumptions) {
                if (is_eq(a) && num_children(a) == 2) {
                    const smt::Term lhs = get_child(a, 0);
                    const auto mv = get_value(lhs);
                    ALOG(5, "model_fix: repaired value %s = %s",
                         lhs->to_string().c_str(),
                         mv ? (*mv)->to_string().c_str() : "?");
                }
            }
            const auto pv = get_value(pure);
            const auto tv = get_value(t);
            ALOG(5, "model_fix: repaired pure %s = %s, term value = %s",
                 pure->to_string().c_str(), pv ? (*pv)->to_string().c_str() : "?",
                 tv ? (*tv)->to_string().c_str() : "?");
        }

        HasUninterpreted hu(_ctx);
        CheckVal cv(_ctx, hu, _pures, _ctx.solver);
        if (cv.check(_current_pure_body)) {
            STATS.model_fix_successes += 1;
            ALOG(2, "model_fix: repaired model");
            return true;
        }
        ALOG(5, "model_fix: candidate satisfied LIA but not NIA");
        restore_model();
        return false;
    };

    for (const auto& pure : info.wrong_pures) {
        const smt::Term& t = _pures.get_t(pure);
        if (!is_mul(t)) {
            ALOG(5, "model_fix: skip %s, original term is not mul: %s",
                 pure->to_string().c_str(), t->to_string().c_str());
            continue;
        }

        const auto pure_val_opt = get_value(pure);
        if (!pure_val_opt || !is_int_value(*pure_val_opt)) {
            ALOG(5, "model_fix: skip %s, pure value is unavailable/non-int",
                 pure->to_string().c_str());
            continue;
        }
        const boost::multiprecision::cpp_int pure_val = term_to_cpp_int(*pure_val_opt);

        const auto adj_it = info.adjustable_vars.find(pure);
        if (adj_it == info.adjustable_vars.end()) {
            ALOG(5, "model_fix: skip %s, no adjustable-variable entry",
                 pure->to_string().c_str());
            continue;
        }
        if (g_verbosity >= 5)
            ALOG(5, "model_fix: candidate pure=%s term=%s pure_value=%s adjustable={%s}",
                 pure->to_string().c_str(), t->to_string().c_str(),
                 (*pure_val_opt)->to_string().c_str(),
                 terms_to_string(adj_it->second).c_str());
        const smt::UnorderedTermSet adjustable(adj_it->second.begin(),
                                               adj_it->second.end());

        const MulSplit split = split_mul(t);
        if (!is_one(_ctx, split.coeff) || split.pows.size() != 2) {
            ALOG(5, "model_fix: skip %s, expected unit-coefficient two-factor mul",
                 pure->to_string().c_str());
            continue;
        }

        const smt::Term& x = split.pows[0][0];
        const smt::Term& y = split.pows[1][0];
        const int k = static_cast<int>(split.pows[0].size());
        const int l = static_cast<int>(split.pows[1].size());
        ALOG(5, "model_fix: split as (%s)^%d * (%s)^%d",
             x->to_string().c_str(), k, y->to_string().c_str(), l);
        if (!adjustable.count(x) || !adjustable.count(y)) {
            ALOG(5, "model_fix: skip two-adjustable repairs for %s, at least one "
                     "split root is not adjustable",
                 pure->to_string().c_str());
        }

        const auto eq = [&](const smt::Term& var,
                            const boost::multiprecision::cpp_int& val) {
            return _ctx.solver->make_term(smt::Equal, var, cpp_int_to_term(_ctx, val));
        };

        // t = x^k*y^1: keep the higher-power side harmless and put [t]'s
        // current value into the linear side.
        if (adjustable.count(x) && adjustable.count(y) && l == 1) {
            if (try_fix("linear-right", pure, t, {eq(x, 1), eq(y, pure_val)}))
                return true;
        }
        if (adjustable.count(x) && adjustable.count(y) && k == 1) {
            if (try_fix("linear-left", pure, t, {eq(y, 1), eq(x, pure_val)}))
                return true;
        }

        // If [t]'s value is an exact k-th or l-th power, assign that root and
        // set the other adjustable factor to 1.
        if (adjustable.count(x) && adjustable.count(y)) {
            if (const auto root = exact_integer_root(pure_val, k)) {
                ALOG(5, "model_fix: %s is an exact %d-th power, root=%s",
                     (*pure_val_opt)->to_string().c_str(), k, root->str().c_str());
                if (try_fix("exact-left-power", pure, t, {eq(x, *root), eq(y, 1)}))
                    return true;
            } else {
                ALOG(5, "model_fix: %s is not an exact %d-th power",
                     (*pure_val_opt)->to_string().c_str(), k);
            }
            if (const auto root = exact_integer_root(pure_val, l)) {
                ALOG(5, "model_fix: %s is an exact %d-th power, root=%s",
                     (*pure_val_opt)->to_string().c_str(), l, root->str().c_str());
                if (try_fix("exact-right-power", pure, t, {eq(y, *root), eq(x, 1)}))
                    return true;
            } else {
                ALOG(5, "model_fix: %s is not an exact %d-th power",
                     (*pure_val_opt)->to_string().c_str(), l);
            }
        }

        const auto try_one_adjustable = [&](const char* technique,
                                            const smt::Term& adjustable_root,
                                            const int adjustable_exp,
                                            const smt::Term& fixed_root,
                                            const int fixed_exp) {
            if (!adjustable.count(adjustable_root) || adjustable.count(fixed_root))
                return false;

            const auto fixed_val = get_value(fixed_root);
            if (!fixed_val || !is_int_value(*fixed_val)) {
                ALOG(5, "model_fix: %s unavailable fixed value for %s", technique,
                     fixed_root->to_string().c_str());
                return false;
            }

            const boost::multiprecision::cpp_int fixed_pow =
                pow_cpp_int(term_to_cpp_int(*fixed_val), fixed_exp);
            if (fixed_pow == 0 || pure_val % fixed_pow != 0) {
                ALOG(5, "model_fix: %s cannot divide %s by fixed factor value %s",
                     technique, (*pure_val_opt)->to_string().c_str(),
                     fixed_pow.str().c_str());
                return false;
            }

            const boost::multiprecision::cpp_int quotient = pure_val / fixed_pow;
            const auto root = exact_integer_root(quotient, adjustable_exp);
            if (!root) {
                ALOG(5, "model_fix: %s quotient %s is not an exact %d-th power",
                     technique, quotient.str().c_str(), adjustable_exp);
                return false;
            }

            ALOG(5, "model_fix: %s quotient=%s root=%s", technique,
                 quotient.str().c_str(), root->str().c_str());
            return try_fix(technique, pure, t, {eq(adjustable_root, *root)});
        };

        if (try_one_adjustable("one-adjustable-left", x, k, y, l))
            return true;
        if (try_one_adjustable("one-adjustable-right", y, l, x, k))
            return true;
    }

    return false;
}

// ── split_mul ─────────────────────────────────────────────────────────────────
LiaAbstraction::MulSplit LiaAbstraction::split_mul(const smt::Term& t) const {
    assert(is_mul(t));
    // Flatten nested muls so that x*(x*x) is recognised as x^3, not x*(x^2).
    smt::TermVec coeffs;
    std::unordered_map<smt::Term, smt::TermVec> pows;
    std::vector<smt::Term> stk;
    for (auto it = t->begin(); it != t->end(); ++it)
        stk.push_back(*it);
    while (!stk.empty()) {
        auto c = stk.back();
        stk.pop_back();
        if (is_mul(c)) {
            for (auto it = c->begin(); it != c->end(); ++it)
                stk.push_back(*it);
        } else if (_hu(c)) {
            // Look through pures-for-muls to recover original factors.
            const smt::Term* orig = _pures.find_t(c);
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
    for (auto& [_, v] : pows)
        spl.pows.push_back(v);
    assert(spl.pows.size() >= 1 && spl.pows.size() <= 2);
    return spl;
}

// ── Axiom generation ──────────────────────────────────────────────────────────
smt::TermVec LiaAbstraction::mk_sign_axioms(const smt::Term& pure,
                                            const MulSplit& split) {
    assert(is_one(_ctx, split.coeff));
    smt::TermVec rv;

    // zero-ness: pure==0 ↔ any root is 0
    // Uses split.pows[i][0] (roots) so the formula stays in LIA.
    {
        smt::TermVec zero_disjs;
        for (auto& pw : split.pows)
            zero_disjs.push_back(_ctx.solver->make_term(smt::Equal, pw[0], _ctx.ZERO));
        rv.push_back(_ctx.solver->make_term(
            smt::Equal, mk_or(_ctx, zero_disjs),
            _ctx.solver->make_term(smt::Equal, pure, _ctx.ZERO)));
    }

    std::vector<smt::Term> oddroots, evenroots;
    for (auto& pw : split.pows) {
        if (pw.size() % 2 != 0)
            oddroots.push_back(pw[0]);
        else
            evenroots.push_back(pw[0]);
    }
    smt::Term pure_gt0 = _ctx.solver->make_term(smt::Gt, pure, _ctx.ZERO);
    if (oddroots.size() == 2) {
        assert(evenroots.empty());
        auto& r0 = oddroots[0];
        auto& r1 = oddroots[1];
        smt::Term both_pos =
            mk_and2(_ctx, _ctx.solver->make_term(smt::Gt, r0, _ctx.ZERO),
                    _ctx.solver->make_term(smt::Gt, r1, _ctx.ZERO));
        smt::Term both_neg =
            mk_and2(_ctx, _ctx.solver->make_term(smt::Lt, r0, _ctx.ZERO),
                    _ctx.solver->make_term(smt::Lt, r1, _ctx.ZERO));
        rv.push_back(_ctx.solver->make_term(smt::Equal, mk_or2(_ctx, both_pos, both_neg),
                                            pure_gt0));
    } else if (oddroots.size() == 1) {
        smt::TermVec nonzero;
        for (auto& r : evenroots)
            nonzero.push_back(_ctx.solver->make_term(smt::Distinct, r, _ctx.ZERO));
        nonzero.push_back(_ctx.solver->make_term(smt::Gt, oddroots[0], _ctx.ZERO));
        rv.push_back(
            _ctx.solver->make_term(smt::Equal, mk_and(_ctx, nonzero), pure_gt0));
    } else if (oddroots.empty()) {
        rv.push_back(_ctx.solver->make_term(smt::Ge, pure, _ctx.ZERO));
    }
    return rv;
}

smt::TermVec LiaAbstraction::mk_pow_axioms(const smt::Term& pure,
                                           const MulSplit& split) {
    assert(split.pows.size() == 1);
    auto& pw = split.pows[0];
    auto& root = pw[0];
    int exp = static_cast<int>(pw.size());

    std::optional<smt::Term> root_val_opt = get_value(root);
    if (!root_val_opt)
        return {};
    smt::Term root_val = *root_val_opt;

    smt::TermVec rv;
    if (is_zero(_ctx, root_val)) {
        rv.push_back(_ctx.solver->make_term(
            smt::Equal, _ctx.solver->make_term(smt::Equal, pure, _ctx.ZERO),
            _ctx.solver->make_term(smt::Equal, root, _ctx.ZERO)));
    } else {
        const bool odd = (exp % 2 == 1);
        smt::Term premise = _ctx.solver->make_term(smt::Equal, root, root_val);
        smt::Term tval = eval_exp(_ctx, root_val, exp);
        if (odd) {
            rv.push_back(_ctx.solver->make_term(
                smt::Equal, _ctx.solver->make_term(smt::Equal, pure, tval), premise));
            smt::Term rv1 = eval_exp(_ctx, eval_sum(_ctx, {root_val, _ctx.ONE}), exp);
            rv.push_back(mk_or2(_ctx, _ctx.solver->make_term(smt::Le, pure, tval),
                                _ctx.solver->make_term(smt::Ge, pure, rv1)));
        } else {
            smt::Term premise1 = _ctx.solver->make_term(smt::Equal, root,
                                                        negate_numeral(_ctx, root_val));
            rv.push_back(_ctx.solver->make_term(
                smt::Equal, _ctx.solver->make_term(smt::Equal, pure, tval),
                mk_or2(_ctx, premise, premise1)));
            smt::Term ar =
                is_neg_val(root_val) ? negate_numeral(_ctx, root_val) : root_val;
            smt::Term ar1 = eval_sum(_ctx, {ar, _ctx.ONE});
            smt::Term tv1 = eval_exp(_ctx, ar1, exp);
            rv.push_back(mk_or2(_ctx, _ctx.solver->make_term(smt::Le, pure, tval),
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
            for (auto& ax : mod_ax_mul(_ctx, static_cast<unsigned int>(_opts.modax),
                                       {{root, exp, root_val}}, pure, *pure_val_opt))
                rv.push_back(ax);
    }
    return rv;
}

smt::TermVec LiaAbstraction::mk_mixed_mul_axioms(const smt::Term& pure,
                                                 const MulSplit& split) {
    assert(split.pows.size() == 2);
    auto pw1 = split.pows[0], pw2 = split.pows[1];
    if (!get_value(pw1[0]))
        std::swap(pw1, pw2);
    auto& root1 = pw1[0];
    auto& root2 = pw2[0];
    int exp1 = static_cast<int>(pw1.size());
    int exp2 = static_cast<int>(pw2.size());

    auto root1_val_opt = get_value(root1);
    if (!root1_val_opt)
        return {};
    smt::Term root1_val = *root1_val_opt;
    auto root2_val_opt = get_value(root2);

    smt::TermVec rv;

    // Single-root equality: fix root1, leave root2^exp2 as ppow2 (linear).
    // Fallback to two-root equality when the pure for root2^exp2 is missing.
    if (!is_zero(_ctx, root1_val)) {
        smt::Term coeff1 = exp1 == 1 ? root1_val : eval_exp(_ctx, root1_val, exp1);
        if (exp2 == 1) {
            smt::Term rhs1 = is_one(_ctx, coeff1) ? root2 : mk_mul(_ctx, {coeff1, root2});
            rv.push_back(mk_implies(_ctx,
                _ctx.solver->make_term(smt::Equal, root1, root1_val),
                _ctx.solver->make_term(smt::Equal, pure, rhs1)));
        } else if (const smt::Term* p2 = _pures.find_p(mk_mul(_ctx, pw2))) {
            smt::Term rhs1 = is_one(_ctx, coeff1) ? *p2 : mk_mul(_ctx, {coeff1, *p2});
            rv.push_back(mk_implies(_ctx,
                _ctx.solver->make_term(smt::Equal, root1, root1_val),
                _ctx.solver->make_term(smt::Equal, pure, rhs1)));
        } else if (root2_val_opt) {
            smt::Term coeff2 = eval_exp(_ctx, *root2_val_opt, exp2);
            rv.push_back(mk_implies(_ctx,
                mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, root1, root1_val),
                        _ctx.solver->make_term(smt::Equal, root2, *root2_val_opt)),
                _ctx.solver->make_term(smt::Equal, pure,
                                       eval_mul(_ctx, {coeff1, coeff2}))));
        }
        // else: pure missing and root2 unassigned — skip
    }

    if (!root2_val_opt)
        return rv;

    smt::Term root2_val = *root2_val_opt;

    // Single-root equality: fix root2, leave root1^exp1 as ppow1 (linear).
    // Fallback to two-root equality when the pure for root1^exp1 is missing.
    if (!is_zero(_ctx, root2_val)) {
        smt::Term coeff2 = exp2 == 1 ? root2_val : eval_exp(_ctx, root2_val, exp2);
        if (exp1 == 1) {
            smt::Term rhs2 = is_one(_ctx, coeff2) ? root1 : mk_mul(_ctx, {coeff2, root1});
            rv.push_back(mk_implies(_ctx,
                _ctx.solver->make_term(smt::Equal, root2, root2_val),
                _ctx.solver->make_term(smt::Equal, pure, rhs2)));
        } else if (const smt::Term* p1 = _pures.find_p(mk_mul(_ctx, pw1))) {
            smt::Term rhs2 = is_one(_ctx, coeff2) ? *p1 : mk_mul(_ctx, {coeff2, *p1});
            rv.push_back(mk_implies(_ctx,
                _ctx.solver->make_term(smt::Equal, root2, root2_val),
                _ctx.solver->make_term(smt::Equal, pure, rhs2)));
        } else {
            // root1_val always available here (guaranteed by swap at top)
            smt::Term coeff1 = eval_exp(_ctx, root1_val, exp1);
            rv.push_back(mk_implies(_ctx,
                mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, root1, root1_val),
                        _ctx.solver->make_term(smt::Equal, root2, root2_val)),
                _ctx.solver->make_term(smt::Equal, pure,
                                       eval_mul(_ctx, {coeff1, coeff2}))));
        }
    }
    for (auto& [c, b] :
         combine_lb(_ctx, root1, exp1, root1_val, root2, exp2, root2_val))
        rv.push_back(triple_to_axiom(_ctx, c, b, pure));
    for (auto& [c, b] :
         combine_ub(_ctx, root1, exp1, root1_val, root2, exp2, root2_val))
        rv.push_back(triple_to_axiom(_ctx, c, pure, b));
    if (_opts.modax > 1) {
        auto pv = get_value(pure);
        if (pv)
            for (auto& ax : mod_ax_mul(
                     _ctx, static_cast<unsigned int>(_opts.modax),
                     {{root1, exp1, root1_val}, {root2, exp2, root2_val}}, pure, *pv))
                rv.push_back(ax);
    }
    if (_opts.tangent && exp1 == 1 && exp2 == 1) {
        auto axs0 = mk_tangent_at(root1, root2, pure, root1_val, root2_val);
        rv.insert(rv.end(), axs0.begin(), axs0.end());

        if (_opts.frontier) {
            int64_t a = term_to_int64(root1_val);
            int64_t b = term_to_int64(root2_val);
            auto& fr = _frontiers[pure];

            auto add_extra = [&](int64_t av, int64_t bv) {
                auto axs = mk_tangent_at(root1, root2, pure,
                                         _ctx.make_int(av), _ctx.make_int(bv));
                rv.insert(rv.end(), axs.begin(), axs.end());
            };

            if (a < fr.lx && b < fr.ly) {
                add_extra(a, fr.uy);
                add_extra(fr.ux, b);
                fr = {a, fr.ux, b, fr.uy};
            } else if (a < fr.lx && b > fr.uy) {
                add_extra(a, fr.ly);
                add_extra(fr.ux, b);
                fr = {a, fr.ux, fr.ly, b};
            } else if (a > fr.ux && b > fr.uy) {
                add_extra(a, fr.ly);
                add_extra(fr.lx, b);
                fr = {fr.lx, a, fr.ly, b};
            } else if (a > fr.ux && b < fr.ly) {
                add_extra(a, fr.uy);
                add_extra(fr.lx, b);
                fr = {fr.lx, a, b, fr.uy};
            }
        }
    }
    return rv;
}

smt::TermVec LiaAbstraction::mk_tangent_at(const smt::Term& root1,
                                           const smt::Term& root2,
                                           const smt::Term& pure,
                                           const smt::Term& av,
                                           const smt::Term& bv) {
    // Tangent plane T(x,y) = bv*x + av*y - av*bv for pure = root1 * root2.
    smt::Term ab = eval_mul(_ctx, {av, bv});
    smt::Term tangent_rhs = _ctx.solver->make_term(
        smt::Minus,
        mk_add(_ctx, {mk_mul(_ctx, {bv, root1}), mk_mul(_ctx, {av, root2})}),
        ab);
    auto gt_a = _ctx.solver->make_term(smt::Gt, root1, av);
    auto lt_a = _ctx.solver->make_term(smt::Lt, root1, av);
    auto gt_b = _ctx.solver->make_term(smt::Gt, root2, bv);
    auto lt_b = _ctx.solver->make_term(smt::Lt, root2, bv);
    auto pure_lt = _ctx.solver->make_term(smt::Lt, pure, tangent_rhs);
    auto pure_gt = _ctx.solver->make_term(smt::Gt, pure, tangent_rhs);
    return {
        mk_implies(_ctx, mk_and2(_ctx, gt_a, lt_b), pure_lt),
        mk_implies(_ctx, mk_and2(_ctx, lt_a, gt_b), pure_lt),
        mk_implies(_ctx, mk_and2(_ctx, lt_a, lt_b), pure_gt),
        mk_implies(_ctx, mk_and2(_ctx, gt_a, gt_b), pure_gt),
    };
}

smt::TermVec LiaAbstraction::mk_mul_axioms(const smt::Term& t) {
    MulSplit spl = split_mul(t);
    assert(is_one(_ctx, spl.coeff));
    assert(spl.pows.size() >= 1 && spl.pows.size() <= 2);
    smt::Term pure = _pures.get_p(t);
    return spl.pows.size() == 1 ? mk_pow_axioms(pure, spl)
                                : mk_mixed_mul_axioms(pure, spl);
}

smt::TermVec LiaAbstraction::mk_mod_axiom(const smt::Term& t) {
    auto x = get_child(t, 0), y = get_child(t, 1);
    smt::Term xval = get_value(x).value_or(smt::Term{});
    smt::Term yval = get_value(y).value_or(smt::Term{});
    smt::Term pure = _pures.get_p(t);

    smt::UnorderedTermMap sx, sy;
    if (yval)
        sy[y] = yval;
    if (xval)
        sx[x] = xval;
    smt::Term tsubs_x = do_substitute(_ctx, x, sy);
    smt::Term tsubs_y = do_substitute(_ctx, y, sx);

    smt::TermVec axioms;
    if (xval && !_hu(xval)) {
        smt::Term abs_y = mk_int_abs(_ctx, tsubs_y);
        if (!is_neg_val(xval)) {
            axioms.push_back(
                mk_implies(_ctx,
                           mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, x, xval),
                                   _ctx.solver->make_term(smt::Gt, abs_y, xval)),
                           _ctx.solver->make_term(smt::Equal, pure, xval)));
        } else {
            smt::Term neg_xval = negate_numeral(_ctx, xval);
            axioms.push_back(mk_implies(
                _ctx,
                mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, x, xval),
                        _ctx.solver->make_term(smt::Gt, abs_y, neg_xval)),
                _ctx.solver->make_term(
                    smt::Equal, pure, _ctx.solver->make_term(smt::Plus, xval, abs_y))));
        }
    }
    if (yval && !_hu(yval) && !is_zero(_ctx, yval)) {
        smt::Term rhs = _ctx.solver->make_term(smt::Mod, tsubs_x, yval);
        axioms.push_back(mk_implies(_ctx, _ctx.solver->make_term(smt::Equal, y, yval),
                                    _ctx.solver->make_term(smt::Equal, pure, rhs)));
    }
    return axioms;
}

smt::TermVec LiaAbstraction::mk_idiv_axiom(const smt::Term& t) {
    auto x = get_child(t, 0), y = get_child(t, 1);
    smt::Term xval = get_value(x).value_or(smt::Term{});
    smt::Term yval = get_value(y).value_or(smt::Term{});
    smt::Term pure = _pures.get_p(t);

    smt::UnorderedTermMap sx, sy;
    if (yval)
        sy[y] = yval;
    if (xval)
        sx[x] = xval;
    smt::Term tsubs_x = do_substitute(_ctx, x, sy);
    smt::Term tsubs_y = do_substitute(_ctx, y, sx);

    smt::TermVec axioms;
    if (xval && !_hu(xval)) {
        smt::Term abs_y = mk_int_abs(_ctx, tsubs_y);
        if (!is_neg_val(xval)) {
            axioms.push_back(
                mk_implies(_ctx,
                           mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, x, xval),
                                   _ctx.solver->make_term(smt::Gt, abs_y, xval)),
                           _ctx.solver->make_term(smt::Equal, pure, _ctx.ZERO)));
        } else {
            smt::Term neg_xval = negate_numeral(_ctx, xval);
            smt::Term ite = _ctx.solver->make_term(
                smt::Ite, _ctx.solver->make_term(smt::Gt, tsubs_y, _ctx.ZERO),
                _ctx.MIN_ONE, _ctx.ONE);
            axioms.push_back(
                mk_implies(_ctx,
                           mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, x, xval),
                                   _ctx.solver->make_term(smt::Ge, abs_y, neg_xval)),
                           _ctx.solver->make_term(smt::Equal, pure, ite)));
        }
    }
    if (yval && !_hu(yval) && !is_zero(_ctx, yval)) {
        smt::Term rhs = _ctx.solver->make_term(smt::IntDiv, tsubs_x, yval);
        axioms.push_back(mk_implies(_ctx, _ctx.solver->make_term(smt::Equal, y, yval),
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
        auto ax = get_child(ta, 0), ay = get_child(ta, 1);
        auto bx = get_child(tb, 0), by = get_child(tb, 1);
        if (auto* p = _pures.find_p(ax))
            ax = *p;
        if (auto* p = _pures.find_p(ay))
            ay = *p;
        if (auto* p = _pures.find_p(bx))
            bx = *p;
        if (auto* p = _pures.find_p(by))
            by = *p;
        return {mk_implies(_ctx,
                           mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, ax, bx),
                                   _ctx.solver->make_term(smt::Equal, ay, by)),
                           _ctx.solver->make_term(smt::Equal, a, b))};
    }
    if (is_mul(ta)) {
        MulSplit spla = split_mul(ta);
        MulSplit splb = split_mul(_pures.get_t(b));
        smt::TermVec axioms;
        if (spla.pows.size() == 2 && splb.pows.size() == 2 &&
            is_one(_ctx, spla.coeff) && is_one(_ctx, splb.coeff)) {
            auto [ar1, ae1] = std::make_pair(spla.pows[0][0], (int)spla.pows[0].size());
            auto [ar2, ae2] = std::make_pair(spla.pows[1][0], (int)spla.pows[1].size());
            auto [br1, be1] = std::make_pair(splb.pows[0][0], (int)splb.pows[0].size());
            auto [br2, be2] = std::make_pair(splb.pows[1][0], (int)splb.pows[1].size());
            if (ae1 == be1 && ae2 == be2)
                axioms.push_back(mk_implies(
                    _ctx,
                    mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, ar1, br1),
                            _ctx.solver->make_term(smt::Equal, ar2, br2)),
                    _ctx.solver->make_term(smt::Equal, a, b)));
            if (ae1 == be2 && ae2 == be1)
                axioms.push_back(mk_implies(
                    _ctx,
                    mk_and2(_ctx, _ctx.solver->make_term(smt::Equal, ar1, br2),
                            _ctx.solver->make_term(smt::Equal, ar2, br1)),
                    _ctx.solver->make_term(smt::Equal, a, b)));
        }
        if (spla.pows.size() == 1 && splb.pows.size() == 1 &&
            is_one(_ctx, spla.coeff) && is_one(_ctx, splb.coeff)) {
            auto& ra = spla.pows[0][0];
            int ea = static_cast<int>(spla.pows[0].size());
            auto& rb = splb.pows[0][0];
            int eb = static_cast<int>(splb.pows[0].size());
            if (ea == eb) {
                smt::Term nra = _ctx.solver->make_term(smt::Negate, ra);
                smt::Term nrb = _ctx.solver->make_term(smt::Negate, rb);
                if (ea % 2 == 1) {
                    axioms.push_back(_ctx.solver->make_term(
                        smt::Equal, _ctx.solver->make_term(smt::Le, ra, rb),
                        _ctx.solver->make_term(smt::Le, a, b)));
                    axioms.push_back(_ctx.solver->make_term(
                        smt::Equal, _ctx.solver->make_term(smt::Le, rb, ra),
                        _ctx.solver->make_term(smt::Le, b, a)));
                } else {
                    // Four quadrant monotonicity axioms (even power).
                    auto make_quad = [&](smt::Term cond, smt::Term lhs_le_rhs,
                                         smt::Term a_le_b) {
                        return mk_implies(
                            _ctx, cond,
                            _ctx.solver->make_term(smt::Equal, lhs_le_rhs, a_le_b));
                    };
                    smt::Term ra0 = _ctx.solver->make_term(smt::Ge, ra, _ctx.ZERO);
                    smt::Term rb0 = _ctx.solver->make_term(smt::Ge, rb, _ctx.ZERO);
                    smt::Term ra1 = _ctx.solver->make_term(smt::Le, ra, _ctx.ZERO);
                    smt::Term rb1 = _ctx.solver->make_term(smt::Le, rb, _ctx.ZERO);
                    axioms.push_back(make_quad(mk_and2(_ctx, ra0, rb0),
                                               _ctx.solver->make_term(smt::Le, ra, rb),
                                               _ctx.solver->make_term(smt::Le, a, b)));
                    axioms.push_back(make_quad(mk_and2(_ctx, ra0, rb0),
                                               _ctx.solver->make_term(smt::Le, rb, ra),
                                               _ctx.solver->make_term(smt::Le, b, a)));
                    axioms.push_back(make_quad(mk_and2(_ctx, ra1, rb1),
                                               _ctx.solver->make_term(smt::Le, rb, ra),
                                               _ctx.solver->make_term(smt::Le, a, b)));
                    axioms.push_back(make_quad(mk_and2(_ctx, ra1, rb1),
                                               _ctx.solver->make_term(smt::Le, ra, rb),
                                               _ctx.solver->make_term(smt::Le, b, a)));
                    axioms.push_back(make_quad(mk_and2(_ctx, ra0, rb1),
                                               _ctx.solver->make_term(smt::Le, ra, nrb),
                                               _ctx.solver->make_term(smt::Le, a, b)));
                    axioms.push_back(make_quad(mk_and2(_ctx, ra0, rb1),
                                               _ctx.solver->make_term(smt::Le, nrb, ra),
                                               _ctx.solver->make_term(smt::Le, b, a)));
                    axioms.push_back(make_quad(mk_and2(_ctx, ra1, rb0),
                                               _ctx.solver->make_term(smt::Le, nra, rb),
                                               _ctx.solver->make_term(smt::Le, a, b)));
                    axioms.push_back(make_quad(mk_and2(_ctx, ra1, rb0),
                                               _ctx.solver->make_term(smt::Le, rb, nra),
                                               _ctx.solver->make_term(smt::Le, b, a)));
                }
            }
        }
        return axioms;
    }
    return {};
}

void LiaAbstraction::add_lazy_congruence_axioms(const CollectPures& pcol) {
    auto process = [&](const smt::UnorderedTermSet& collection) {
        for (auto it1 = collection.begin(); it1 != collection.end(); ++it1) {
            for (auto it2 = std::next(it1); it2 != collection.end(); ++it2) {
                auto lo = std::min(*it1, *it2);
                auto hi = std::max(*it1, *it2);
                auto key = std::make_pair(lo, hi);
                if (_congruence_pairs_added.count(key))
                    continue;
                auto candidates = congruence_axioms_for_pair(*it1, *it2);
                smt::TermVec violated_axs;
                for (auto& ax : candidates) {
                    auto v = get_value(ax);
                    if (v && is_false(_ctx, *v))
                        violated_axs.push_back(ax);
                }
                if (!violated_axs.empty()) {
                    add_axioms(*it1, candidates, "cong");
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
    ALOG(4, "check_nia: %s == %s", pure->to_string().c_str(), t->to_string().c_str());
    auto pure_val_opt = get_value(pure);
    if (!pure_val_opt)
        return true;
    smt::Term pure_val = *pure_val_opt;

    // Division by zero: keep the current interpretation.
    if ((is_mod(t) || is_idiv(t))) {
        smt::Term den = get_value(get_child(t, 1)).value_or(smt::Term{});
        if (den && is_zero(_ctx, den)) {
            smt::Term num = get_value(get_child(t, 0)).value_or(smt::Term{});
            if (is_mod(t) && num)
                _mod_zero_interp[num] = pure_val;
            if (is_idiv(t) && num)
                _idiv_zero_interp[num] = pure_val;
            return true;
        }
    }

    auto tval_opt = get_value(t);
    if (!tval_opt)
        return true;
    smt::Term tval = *tval_opt;
    ALOG(4, "check_nia: --> %s == %s", pure_val->to_string().c_str(),
         tval->to_string().c_str());
    return pure_val == tval;
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
            ALOG(3, "  %s = %s", pure->to_string().c_str(),
                 pv ? (*pv)->to_string().c_str() : "?");
        }
    }

    // Quick three-valued check.
    HasUninterpreted hu(_ctx);
    CheckVal cv(_ctx, hu, _pures, _ctx.solver);
    if (cv.check(_current_pure_body)) {
        ALOG(2, "check_nia quick ok");
        return true;
    }
    // Implicant-based targeted refinement.
    //
    // collect_implicant gives exactly the literals sufficient to satisfy the
    // formula under the current LIA model. For each literal we substitute
    // each pure with its NIA value and re-evaluate: if the literal is still
    // true (or unknown) we need no axioms for it. We collect pures only from
    // literals that evaluate to FALSE under NIA semantics.
    //
    // Key early-exit: if we got an implicant and every literal is NIA-ok,
    // the formula is already NIA-satisfied — return true without any axioms.
    // Falls back to full pcol when collect_implicant fails.
    //
    // When --model-fix is active, model_fix_info already computes the implicant
    // internally — we reuse it rather than recomputing.
    smt::TermVec implicant;
    bool got_implicant = false;

    if (_opts.model_fix || _opts.model_fix2) {
        ScopedPhase mf_sp(STATS.model_fix_time);
        const auto fix_info = cv.model_fix_info(_current_pure_body);
        ALOG(3, "model_fix: implicant=%zu wrong_pures=%zu relevant_vars=%zu",
             fix_info.implicant.size(), fix_info.wrong_pures.size(),
             fix_info.relevant_vars.size());
        if (g_verbosity >= 4) {
            for (const auto& lit : fix_info.implicant)
                ALOG(4, "model_fix: L %s", lit->to_string().c_str());
            for (const auto& pure : fix_info.wrong_pures) {
                const auto it = fix_info.adjustable_vars.find(pure);
                const std::string vars =
                    it != fix_info.adjustable_vars.end() ? terms_to_string(it->second)
                                                         : "";
                ALOG(4, "model_fix: W %s adjustable={%s}",
                     pure->to_string().c_str(), vars.c_str());
            }
            if (!fix_info.relevant_vars.empty()) {
                smt::TermVec rv(fix_info.relevant_vars.begin(),
                                fix_info.relevant_vars.end());
                ALOG(4, "model_fix: relevant_vars={%s}", terms_to_string(rv).c_str());
            }
        }
        if (apply_model_fix_sub(fix_info, _opts.model_fix2 ? -1 : 5))
            return true;
        if (apply_model_fix(fix_info))
            return true;
        // Model-fix failed. Don't reuse fix_info.implicant: the failure-restore
        // path in apply_model_fix_sub may have put a different model in main_slv
        // (via main_slv.reset()+lia_expr+check()), making the stale implicant
        // inconsistent with the current model — pure-free literals that are now
        // FALSE would be silently skipped, causing a spurious "NIA ok".
    }
    got_implicant = cv.collect_implicant(_current_pure_body, implicant);

    CollectPures targeted_pcol(_ctx, _pures, _axioms);
    if (got_implicant) {
        // Pre-compute NIA values for all active pures.
        smt::UnorderedTermMap nia_vals;
        for (const auto& pure : pcol.collected) {
            auto nv = get_value(_pures.get_t(pure));
            if (nv) nia_vals[pure] = *nv;
        }

        // Collect pures directly visible in t (stops at pcol.collected members).
        auto direct_pures_in = [&](const smt::Term& t) {
            smt::TermVec result;
            smt::UnorderedTermSet seen;
            std::vector<smt::Term> stk = {t};
            while (!stk.empty()) {
                smt::Term node = stk.back();
                stk.pop_back();
                if (!seen.insert(node).second)
                    continue;
                if (pcol.collected.count(node)) {
                    result.push_back(node);
                } else {
                    for (auto it = node->begin(); it != node->end(); ++it)
                        stk.push_back(*it);
                }
            }
            return result;
        };

        for (const auto& lit : implicant) {
            const auto pures_in_lit = direct_pures_in(lit);
            if (pures_in_lit.empty())
                continue;
            smt::UnorderedTermMap subst;
            bool any_wrong = false;
            for (const auto& p : pures_in_lit) {
                auto it = nia_vals.find(p);
                if (it == nia_vals.end())
                    continue;
                subst[p] = it->second;
                auto lv = get_value(p);
                if (!lv || !(*lv == it->second))
                    any_wrong = true;
            }
            if (!any_wrong)
                continue;  // all pures in this literal match NIA — no axioms needed
            const smt::Term subst_lit = do_substitute(_ctx, lit, subst);
            auto val = get_value(subst_lit);
            if (val && is_false(_ctx, *val))
                targeted_pcol(lit);
        }

        if (targeted_pcol.collected.empty()) {
            ALOG(2, "check_nia ok (all implicant literals NIA-satisfied)");
            return true;
        }
    }

    const bool use_targeted = got_implicant;
    const CollectPures& active_pcol = use_targeted ? targeted_pcol : pcol;
    if (use_targeted)
        STATS.skipped_pures += static_cast<long>(pcol.collected.size()) -
                               static_cast<long>(targeted_pcol.collected.size());
    ALOG(3, "check_nia: targeted=%d active=%zu/%zu", use_targeted,
         active_pcol.collected.size(), pcol.collected.size());

    bool res = true;

    size_t pairs_before = _congruence_pairs_added.size();
    if (_opts.congruence)
        add_lazy_congruence_axioms(active_pcol);
    if (_congruence_pairs_added.size() > pairs_before)
        res = false;

    for (auto& pure : active_pcol.collected) {
        const smt::Term& t = _pures.get_t(pure);
        if (is_okay(pure, t))
            continue;

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
            if (!_sign_axioms_added.count(pure)) {
                MulSplit spl = split_mul(t);
                add_axioms(pure, mk_sign_axioms(pure, spl), "smul");
                _sign_axioms_added.insert(pure);
            }
            auto axs = mk_mul_axioms(t);
            add_axioms(pure, axs, "mul");
            STATS.mul_axioms += static_cast<long>(axs.size());
        }
    }
    return res;
}
