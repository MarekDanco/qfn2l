#include "utils.h"
#include <algorithm>
#include <cassert>
#include <regex>
#include <stdexcept>

#ifdef BACKEND_Z3
#  include "z3_solver.h"
#  include "z3_term.h"
#endif

// ── Ctx ───────────────────────────────────────────────────────────────────────
Ctx::Ctx(smt::SmtSolver s) : solver(std::move(s)) {
    int_sort  = solver->make_sort(smt::INT);
    bool_sort = solver->make_sort(smt::BOOL);
    ZERO    = solver->make_term(int64_t(0),  int_sort);
    ONE     = solver->make_term(int64_t(1),  int_sort);
    MIN_ONE = solver->make_term(int64_t(-1), int_sort);
    TRUE_T  = solver->make_term(true);
    FALSE_T = solver->make_term(false);
}

smt::Term Ctx::make_int(int64_t n) const {
    return solver->make_term(n, int_sort);
}

smt::Term Ctx::make_int_str(const std::string& s) const {
    static const std::regex neg_re(R"(\(\s*-\s*(\d+)\s*\))");
    std::smatch m;
    if (std::regex_match(s, m, neg_re)) {
        smt::Term pos = solver->make_term(m[1].str(), int_sort);
        return solver->make_term(smt::Negate, pos);
    }
    return solver->make_term(s, int_sort);
}

smt::Term Ctx::fresh_symbol(const smt::Sort& sort,
                             const std::string& prefix) const {
    int id = _fresh_ctr.fetch_add(1);
    return solver->make_symbol(prefix + std::to_string(id), sort);
}

// ── Numeral helpers ───────────────────────────────────────────────────────────
int64_t term_to_int64(const smt::Term& t) {
    std::string s = t->to_string();
    static const std::regex neg_re(R"(\(\s*-\s*(\d+)\s*\))");
    std::smatch m;
    if (std::regex_match(s, m, neg_re))
        return -static_cast<int64_t>(std::stoull(m[1].str()));
    return static_cast<int64_t>(std::stoull(s));
}

// Simplify a pure-numeral expression to a single numeral term via z3 constant folding.
static smt::Term numeral_simplify(const Ctx& ctx, const smt::Term& t) {
#ifdef BACKEND_Z3
    auto* z3t = dynamic_cast<smt::Z3Term*>(t.get());
    if (z3t) {
        std::string s = z3t->get_z3_expr().simplify().to_string();
        return ctx.make_int_str(s);
    }
#endif
    return t;
}

bool is_value(const smt::Term& t)      { return t->is_value(); }
bool is_int_value(const smt::Term& t)  {
    return t->is_value() && t->get_sort()->get_sort_kind() == smt::INT;
}
bool is_symbol(const smt::Term& t)     { return t->is_symbol(); }
bool is_symbolic_const(const smt::Term& t) {
    return t->is_symbolic_const();
}

// ── Op-code predicates ────────────────────────────────────────────────────────
bool is_app(const smt::Term& t) {
    return !t->get_op().is_null();
}
bool is_app_of(const smt::Term& t, smt::PrimOp op) {
    return is_app(t) && t->get_op().prim_op == op;
}
bool is_mul(const smt::Term& t)      { return is_app_of(t, smt::Mult); }
bool is_add(const smt::Term& t)      { return is_app_of(t, smt::Plus); }
bool is_sub(const smt::Term& t)      { return is_app_of(t, smt::Minus); }
bool is_mod(const smt::Term& t)      { return is_app_of(t, smt::Mod); }
bool is_idiv(const smt::Term& t)     { return is_app_of(t, smt::IntDiv); }
bool is_and(const smt::Term& t)      { return is_app_of(t, smt::And); }
bool is_or(const smt::Term& t)       { return is_app_of(t, smt::Or); }
bool is_not(const smt::Term& t)      { return is_app_of(t, smt::Not); }
bool is_eq(const smt::Term& t)       { return is_app_of(t, smt::Equal); }
bool is_distinct(const smt::Term& t) { return is_app_of(t, smt::Distinct); }
bool is_le(const smt::Term& t)       { return is_app_of(t, smt::Le); }
bool is_lt(const smt::Term& t)       { return is_app_of(t, smt::Lt); }
bool is_ge(const smt::Term& t)       { return is_app_of(t, smt::Ge); }
bool is_gt(const smt::Term& t)       { return is_app_of(t, smt::Gt); }
bool is_implies(const smt::Term& t)  { return is_app_of(t, smt::Implies); }
bool is_ite(const smt::Term& t)      { return is_app_of(t, smt::Ite); }
bool is_quantifier(const smt::Term& t) {
    return is_app_of(t, smt::Forall) || is_app_of(t, smt::Exists);
}
bool is_forall(const smt::Term& t)   { return is_app_of(t, smt::Forall); }
bool is_exists(const smt::Term& t)   { return is_app_of(t, smt::Exists); }

bool is_int(const Ctx& ctx, const smt::Term& t) {
    return t->get_sort() == ctx.int_sort;
}
bool is_bool(const Ctx& ctx, const smt::Term& t) {
    return t->get_sort() == ctx.bool_sort;
}

bool is_int_atom(const Ctx& ctx, const smt::Term& t) {
    if (is_le(t) || is_lt(t) || is_ge(t) || is_gt(t)) return true;
    if (is_eq(t) || is_distinct(t)) {
        auto it = t->begin();
        if (it != t->end()) return (*it)->get_sort() == ctx.int_sort;
    }
    return false;
}

bool is_non_linear(const smt::Term& t) {
    if (!is_mul(t)) return false;
    int symbolic = 0;
    for (auto it = t->begin(); it != t->end(); ++it)
        if (!is_value(*it)) ++symbolic;
    return symbolic > 1;
}

bool is_nnf_connective(const smt::Term& t) {
    return is_and(t) || is_or(t) || is_not(t);
}

// ── Value checks ─────────────────────────────────────────────────────────────
bool is_true(const Ctx& ctx, const smt::Term& t)    { return t == ctx.TRUE_T; }
bool is_false(const Ctx& ctx, const smt::Term& t)   { return t == ctx.FALSE_T; }
bool is_zero(const Ctx& ctx, const smt::Term& t)    {
    return t->is_value() && t->get_sort() == ctx.int_sort && t->to_string() == "0";
}
bool is_one(const Ctx& ctx, const smt::Term& t)     {
    return t->is_value() && t->get_sort() == ctx.int_sort && t->to_string() == "1";
}
bool is_min_one(const Ctx& ctx, const smt::Term& t) {
    if (!t->is_value() || t->get_sort() != ctx.int_sort) return false;
    std::string s = t->to_string();
    static const std::regex neg_one_re(R"(\(\s*-\s*1\s*\))");
    return std::regex_match(s, neg_one_re);
}
bool is_neg_val(const smt::Term& t) {
    if (!t->is_value()) return false;
    std::string s = t->to_string();
    // SMT-LIB negative integers appear as "(- N)"
    return !s.empty() && s[0] == '(';
}

// ── Term building ─────────────────────────────────────────────────────────────
smt::Term mk_true(const Ctx& ctx)  { return ctx.TRUE_T; }
smt::Term mk_false(const Ctx& ctx) { return ctx.FALSE_T; }

smt::Term mk_not(const Ctx& ctx, const smt::Term& a) {
    if (is_not(a)) return get_child(a, 0);
    if (is_true(ctx, a))  return ctx.FALSE_T;
    if (is_false(ctx, a)) return ctx.TRUE_T;
    return ctx.solver->make_term(smt::Not, a);
}

smt::Term mk_and(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.TRUE_T;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (is_false(ctx, a)) return ctx.FALSE_T;
        if (!is_true(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty()) return ctx.TRUE_T;
    if (filtered.size() == 1) return filtered[0];
    return ctx.solver->make_term(smt::And, filtered);
}

smt::Term mk_or(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.FALSE_T;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (is_true(ctx, a))  return ctx.TRUE_T;
        if (!is_false(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty()) return ctx.FALSE_T;
    if (filtered.size() == 1) return filtered[0];
    return ctx.solver->make_term(smt::Or, filtered);
}

smt::Term mk_and2(const Ctx& ctx, const smt::Term& a, const smt::Term& b) {
    return mk_and(ctx, {a, b});
}
smt::Term mk_or2(const Ctx& ctx, const smt::Term& a, const smt::Term& b) {
    return mk_or(ctx, {a, b});
}
smt::Term mk_implies(const Ctx& ctx, const smt::Term& a, const smt::Term& b) {
    return ctx.solver->make_term(smt::Implies, a, b);
}

smt::Term mk_mul(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.ONE;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (is_zero(ctx, a)) return ctx.ZERO;
        if (!is_one(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty()) return ctx.ONE;
    if (filtered.size() == 1) return filtered[0];
    smt::Term r = ctx.solver->make_term(smt::Mult, filtered[0], filtered[1]);
    for (size_t i = 2; i < filtered.size(); ++i)
        r = ctx.solver->make_term(smt::Mult, r, filtered[i]);
    return r;
}

smt::Term mk_add(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.ZERO;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (!is_zero(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty()) return ctx.ZERO;
    if (filtered.size() == 1) return filtered[0];
    smt::Term r = ctx.solver->make_term(smt::Plus, filtered[0], filtered[1]);
    for (size_t i = 2; i < filtered.size(); ++i)
        r = ctx.solver->make_term(smt::Plus, r, filtered[i]);
    return r;
}

int64_t term_mod_int(const Ctx& ctx, const smt::Term& val, int64_t k) {
    try {
        int64_t v = term_to_int64(val);
        return ((v % k) + k) % k;
    } catch (const std::out_of_range&) {}
    smt::Term modterm = ctx.solver->make_term(smt::Mod, val, ctx.make_int(k));
    return term_to_int64(numeral_simplify(ctx, modterm));
}

smt::Term eval_mul(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.ONE;
    for (auto& a : args) assert(a->is_value());
    if (args.size() == 1) return args[0];
    try {
        int64_t r = 1;
        for (auto& a : args) r = r * term_to_int64(a);  // throws on overflow
        return ctx.make_int(r);
    } catch (const std::out_of_range&) {}
    smt::Term t = ctx.solver->make_term(smt::Mult, args[0], args[1]);
    for (size_t i = 2; i < args.size(); ++i)
        t = ctx.solver->make_term(smt::Mult, t, args[i]);
    return numeral_simplify(ctx, t);
}

smt::Term eval_sum(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.ZERO;
    for (auto& a : args) assert(a->is_value());
    if (args.size() == 1) return args[0];
    try {
        int64_t r = 0;
        for (auto& a : args) r = r + term_to_int64(a);
        return ctx.make_int(r);
    } catch (const std::out_of_range&) {}
    smt::Term t = ctx.solver->make_term(smt::Plus, args[0], args[1]);
    for (size_t i = 2; i < args.size(); ++i)
        t = ctx.solver->make_term(smt::Plus, t, args[i]);
    return numeral_simplify(ctx, t);
}

smt::Term eval_exp(const Ctx& ctx, const smt::Term& x, int n) {
    assert(n >= 0);
    assert(x->is_value());
    if (n == 0) return ctx.ONE;
    try {
        int64_t v = term_to_int64(x), r = 1;
        for (int i = 0; i < n; ++i) r = r * v;
        return ctx.make_int(r);
    } catch (const std::out_of_range&) {}
    smt::Term t = x;
    for (int i = 1; i < n; ++i)
        t = ctx.solver->make_term(smt::Mult, t, x);
    return numeral_simplify(ctx, t);
}

smt::Term mk_pow(const Ctx& ctx, const smt::Term& x, int n) {
    assert(n >= 0);
    if (n == 0) return ctx.ONE;
    smt::TermVec factors(n, x);
    return mk_mul(ctx, factors);
}

smt::Term eval_pow(const Ctx& ctx, const smt::Term& x, int n) {
    smt::TermVec factors(n, x);
    return eval_mul(ctx, factors);
}

smt::Term negate_numeral(const Ctx& ctx, const smt::Term& n) {
    assert(n->is_value());
    try { return ctx.make_int(-term_to_int64(n)); } catch (const std::out_of_range&) {}
    return numeral_simplify(ctx, ctx.solver->make_term(smt::Negate, n));
}

// ── Child access ──────────────────────────────────────────────────────────────
smt::TermVec get_children(const smt::Term& t) {
    return smt::TermVec(t->begin(), t->end());
}

size_t num_children(const smt::Term& t) {
    return std::distance(t->begin(), t->end());
}

smt::Term get_child(const smt::Term& t, size_t i) {
    auto it = t->begin();
    std::advance(it, i);
    return *it;
}

smt::Term rebuild(const Ctx& ctx, const smt::Term& t,
                  const smt::TermVec& new_args) {
    return ctx.solver->make_term(t->get_op(), new_args);
}

// ── Variable collection ───────────────────────────────────────────────────────
smt::TermVec get_vars(const smt::Term& root) {
    smt::UnorderedTermSet vars, visited;
    std::vector<smt::Term> stk = {root};
    while (!stk.empty()) {
        smt::Term t = stk.back(); stk.pop_back();
        if (!visited.insert(t).second) continue;
        if (t->is_symbolic_const()) vars.insert(t);
        for (auto it = t->begin(); it != t->end(); ++it)
            stk.push_back(*it);
    }
    return smt::TermVec(vars.begin(), vars.end());
}

// ── Substitution wrappers ─────────────────────────────────────────────────────
smt::Term do_substitute(const Ctx& ctx, const smt::Term& t,
                        const smt::UnorderedTermMap& subs) {
    if (subs.empty()) return t;
    return ctx.solver->substitute(t, subs);
}

smt::Term do_substitute(const Ctx& ctx, const smt::Term& t,
                        const smt::TermVec& from, const smt::TermVec& to) {
    assert(from.size() == to.size());
    if (from.empty()) return t;
    smt::UnorderedTermMap subs;
    for (size_t i = 0; i < from.size(); ++i) subs[from[i]] = to[i];
    return ctx.solver->substitute(t, subs);
}

smt::Term pairs2fla(const Ctx& ctx,
                    const std::vector<std::pair<smt::Term, smt::Term>>& pairs) {
    smt::TermVec eqs;
    for (auto& [e, v] : pairs)
        eqs.push_back(ctx.solver->make_term(smt::Equal, e, v));
    return mk_and(ctx, eqs);
}
