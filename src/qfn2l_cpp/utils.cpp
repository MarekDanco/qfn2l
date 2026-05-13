#include "utils.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <regex>
#include <stdexcept>

#ifdef BACKEND_Z3
#include "z3_solver.h"
#include "z3_term.h"
#endif

using boost::multiprecision::cpp_int;

static std::string trim_copy(const std::string& s) {
    const char* ws = " \t\n\r";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

static cpp_int parse_cpp_int(const std::string& raw) {
    std::string s = trim_copy(raw);
    static const std::regex sexpr_neg_re(R"(\(\s*-\s*(.+?)\s*\))");
    std::smatch m;
    if (std::regex_match(s, m, sexpr_neg_re))
        return -parse_cpp_int(m[1].str());
    return cpp_int(s);
}

// ── Ctx ───────────────────────────────────────────────────────────────────────
Ctx::Ctx(smt::SmtSolver s) : solver(std::move(s)) {
    int_sort  = solver->make_sort(smt::INT);
    bool_sort = solver->make_sort(smt::BOOL);
    ZERO      = solver->make_term(int64_t(0), int_sort);
    ONE       = solver->make_term(int64_t(1), int_sort);
    MIN_ONE   = solver->make_term(int64_t(-1), int_sort);
    TRUE_T    = solver->make_term(true);
    FALSE_T   = solver->make_term(false);
}

smt::Term Ctx::make_int(int64_t n) const { return solver->make_term(n, int_sort); }

smt::Term Ctx::make_int_str(const std::string& s) const {
    return cpp_int_to_term(*this, parse_cpp_int(s));
}

smt::Term Ctx::fresh_symbol(const smt::Sort& sort, const std::string& prefix) const {
    int id = _fresh_ctr.fetch_add(1);
    return solver->make_symbol(prefix + std::to_string(id), sort);
}

// ── Numeral helpers ───────────────────────────────────────────────────────────
cpp_int term_to_cpp_int(const smt::Term& t) {
#ifdef BACKEND_Z3
    auto* z3t = dynamic_cast<smt::Z3Term*>(t.get());
    if (z3t) {
        std::string s;
        if (z3t->get_z3_expr().is_numeral(s))
            return parse_cpp_int(s);
    }
#endif
    return parse_cpp_int(t->to_string());
}

smt::Term cpp_int_to_term(const Ctx& ctx, const cpp_int& v) {
    if (v >= 0)
        return ctx.solver->make_term(v.str(), ctx.int_sort);
    cpp_int abs_v = -v;
    smt::Term pos = ctx.solver->make_term(abs_v.str(), ctx.int_sort);
    return ctx.solver->make_term(smt::Negate, pos);
}

int64_t cpp_int_to_int64(const cpp_int& v) {
    static const cpp_int min = std::numeric_limits<int64_t>::min();
    static const cpp_int max = std::numeric_limits<int64_t>::max();
    if (v < min || v > max)
        throw std::out_of_range("integer does not fit int64_t");
    return v.convert_to<int64_t>();
}

int64_t term_to_int64(const smt::Term& t) {
    return cpp_int_to_int64(term_to_cpp_int(t));
}

bool is_value(const smt::Term& t) { return t->is_value(); }
bool is_int_value(const smt::Term& t) {
    return t->is_value() && t->get_sort()->get_sort_kind() == smt::INT;
}
bool is_symbol(const smt::Term& t) { return t->is_symbol(); }
bool is_symbolic_const(const smt::Term& t) { return t->is_symbolic_const(); }

// ── Op-code predicates ────────────────────────────────────────────────────────
bool is_app(const smt::Term& t) { return !t->get_op().is_null(); }
bool is_app_of(const smt::Term& t, smt::PrimOp op) {
    return is_app(t) && t->get_op().prim_op == op;
}
bool is_mul(const smt::Term& t) { return is_app_of(t, smt::Mult); }
bool is_add(const smt::Term& t) { return is_app_of(t, smt::Plus); }
bool is_sub(const smt::Term& t) { return is_app_of(t, smt::Minus); }
bool is_mod(const smt::Term& t) { return is_app_of(t, smt::Mod); }
bool is_idiv(const smt::Term& t) { return is_app_of(t, smt::IntDiv); }
bool is_and(const smt::Term& t) { return is_app_of(t, smt::And); }
bool is_or(const smt::Term& t) { return is_app_of(t, smt::Or); }
bool is_not(const smt::Term& t) { return is_app_of(t, smt::Not); }
bool is_eq(const smt::Term& t) { return is_app_of(t, smt::Equal); }
bool is_distinct(const smt::Term& t) { return is_app_of(t, smt::Distinct); }
bool is_le(const smt::Term& t) { return is_app_of(t, smt::Le); }
bool is_lt(const smt::Term& t) { return is_app_of(t, smt::Lt); }
bool is_ge(const smt::Term& t) { return is_app_of(t, smt::Ge); }
bool is_gt(const smt::Term& t) { return is_app_of(t, smt::Gt); }
bool is_implies(const smt::Term& t) { return is_app_of(t, smt::Implies); }
bool is_ite(const smt::Term& t) { return is_app_of(t, smt::Ite); }
bool is_quantifier(const smt::Term& t) {
    return is_app_of(t, smt::Forall) || is_app_of(t, smt::Exists);
}
bool is_forall(const smt::Term& t) { return is_app_of(t, smt::Forall); }
bool is_exists(const smt::Term& t) { return is_app_of(t, smt::Exists); }

bool is_int(const Ctx& ctx, const smt::Term& t) {
    return t->get_sort() == ctx.int_sort;
}
bool is_bool(const Ctx& ctx, const smt::Term& t) {
    return t->get_sort() == ctx.bool_sort;
}

bool is_int_atom(const Ctx& ctx, const smt::Term& t) {
    if (is_le(t) || is_lt(t) || is_ge(t) || is_gt(t))
        return true;
    if (is_eq(t) || is_distinct(t)) {
        auto it = t->begin();
        if (it != t->end())
            return (*it)->get_sort() == ctx.int_sort;
    }
    return false;
}

bool is_non_linear(const smt::Term& t) {
    if (!is_mul(t))
        return false;
    int symbolic = 0;
    for (auto it = t->begin(); it != t->end(); ++it)
        if (!is_value(*it))
            ++symbolic;
    return symbolic > 1;
}

bool is_nnf_connective(const smt::Term& t) {
    return is_and(t) || is_or(t) || is_not(t);
}

// ── Value checks ─────────────────────────────────────────────────────────────
bool is_true(const Ctx& ctx, const smt::Term& t) { return t == ctx.TRUE_T; }
bool is_false(const Ctx& ctx, const smt::Term& t) { return t == ctx.FALSE_T; }
bool is_zero(const Ctx& ctx, const smt::Term& t) {
    return t->is_value() && t->get_sort() == ctx.int_sort && t->to_string() == "0";
}
bool is_one(const Ctx& ctx, const smt::Term& t) {
    return t->is_value() && t->get_sort() == ctx.int_sort && t->to_string() == "1";
}
bool is_min_one(const Ctx& ctx, const smt::Term& t) {
    if (!t->is_value() || t->get_sort() != ctx.int_sort)
        return false;
    std::string             s = t->to_string();
    static const std::regex neg_one_re(R"(\(\s*-\s*1\s*\))");
    return std::regex_match(s, neg_one_re);
}
bool is_neg_val(const smt::Term& t) {
    if (!t->is_value())
        return false;
    std::string s = t->to_string();
    // SMT-LIB negative integers appear as "(- N)"
    return !s.empty() && s[0] == '(';
}

// ── Term building ─────────────────────────────────────────────────────────────
smt::Term mk_true(const Ctx& ctx) { return ctx.TRUE_T; }
smt::Term mk_false(const Ctx& ctx) { return ctx.FALSE_T; }

smt::Term mk_not(const Ctx& ctx, const smt::Term& a) {
    if (is_not(a))
        return get_child(a, 0);
    if (is_true(ctx, a))
        return ctx.FALSE_T;
    if (is_false(ctx, a))
        return ctx.TRUE_T;
    return ctx.solver->make_term(smt::Not, a);
}

smt::Term mk_and(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty())
        return ctx.TRUE_T;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (is_false(ctx, a))
            return ctx.FALSE_T;
        if (!is_true(ctx, a))
            filtered.push_back(a);
    }
    if (filtered.empty())
        return ctx.TRUE_T;
    if (filtered.size() == 1)
        return filtered[0];
    return ctx.solver->make_term(smt::And, filtered);
}

smt::Term mk_or(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty())
        return ctx.FALSE_T;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (is_true(ctx, a))
            return ctx.TRUE_T;
        if (!is_false(ctx, a))
            filtered.push_back(a);
    }
    if (filtered.empty())
        return ctx.FALSE_T;
    if (filtered.size() == 1)
        return filtered[0];
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
    if (args.empty())
        return ctx.ONE;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (is_zero(ctx, a))
            return ctx.ZERO;
        if (!is_one(ctx, a))
            filtered.push_back(a);
    }
    if (filtered.empty())
        return ctx.ONE;
    if (filtered.size() == 1)
        return filtered[0];
    smt::Term r = ctx.solver->make_term(smt::Mult, filtered[0], filtered[1]);
    for (size_t i = 2; i < filtered.size(); ++i)
        r = ctx.solver->make_term(smt::Mult, r, filtered[i]);
    return r;
}

smt::Term mk_add(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty())
        return ctx.ZERO;
    smt::TermVec filtered;
    for (auto& a : args) {
        if (!is_zero(ctx, a))
            filtered.push_back(a);
    }
    if (filtered.empty())
        return ctx.ZERO;
    if (filtered.size() == 1)
        return filtered[0];
    smt::Term r = ctx.solver->make_term(smt::Plus, filtered[0], filtered[1]);
    for (size_t i = 2; i < filtered.size(); ++i)
        r = ctx.solver->make_term(smt::Plus, r, filtered[i]);
    return r;
}

int64_t term_mod_int(const Ctx&, const smt::Term& val, int64_t k) {
    cpp_int v = term_to_cpp_int(val);
    cpp_int kk = k;
    cpp_int r = v % kk;
    if (r < 0) r += kk;
    return cpp_int_to_int64(r);
}

smt::Term eval_mul(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.ONE;
    for (auto& a : args) { assert(a->is_value()); (void)a; }
    if (args.size() == 1) return args[0];
    cpp_int r = 1;
    for (auto& a : args)
        r *= term_to_cpp_int(a);
    return cpp_int_to_term(ctx, r);
}

smt::Term eval_sum(const Ctx& ctx, const smt::TermVec& args) {
    if (args.empty()) return ctx.ZERO;
    for (auto& a : args) { assert(a->is_value()); (void)a; }
    if (args.size() == 1) return args[0];
    cpp_int r = 0;
    for (auto& a : args)
        r += term_to_cpp_int(a);
    return cpp_int_to_term(ctx, r);
}

smt::Term eval_exp(const Ctx& ctx, const smt::Term& x, int n) {
    assert(n >= 0);
    assert(x->is_value());
    if (n == 0) return ctx.ONE;
    cpp_int v = term_to_cpp_int(x);
    cpp_int r = 1;
    for (int i = 0; i < n; ++i)
        r *= v;
    return cpp_int_to_term(ctx, r);
}

smt::Term mk_pow(const Ctx& ctx, const smt::Term& x, int n) {
    assert(n >= 0);
    if (n == 0)
        return ctx.ONE;
    smt::TermVec factors(n, x);
    return mk_mul(ctx, factors);
}

smt::Term eval_pow(const Ctx& ctx, const smt::Term& x, int n) {
    smt::TermVec factors(n, x);
    return eval_mul(ctx, factors);
}

smt::Term negate_numeral(const Ctx& ctx, const smt::Term& n) {
    assert(n->is_value());
    return cpp_int_to_term(ctx, -term_to_cpp_int(n));
}

// ── Child access ──────────────────────────────────────────────────────────────
smt::TermVec get_children(const smt::Term& t) {
    return smt::TermVec(t->begin(), t->end());
}

size_t num_children(const smt::Term& t) { return std::distance(t->begin(), t->end()); }

smt::Term get_child(const smt::Term& t, size_t i) {
    auto it = t->begin();
    std::advance(it, i);
    return *it;
}

smt::Term rebuild(const Ctx& ctx, const smt::Term& t, const smt::TermVec& new_args) {
    return ctx.solver->make_term(t->get_op(), new_args);
}

// ── Variable collection ───────────────────────────────────────────────────────
smt::TermVec get_vars(const smt::Term& root) {
    smt::UnorderedTermSet  vars, visited;
    std::vector<smt::Term> stk = {root};
    while (!stk.empty()) {
        smt::Term t = stk.back();
        stk.pop_back();
        if (!visited.insert(t).second)
            continue;
        if (t->is_symbolic_const())
            vars.insert(t);
        for (auto it = t->begin(); it != t->end(); ++it)
            stk.push_back(*it);
    }
    return smt::TermVec(vars.begin(), vars.end());
}

// ── Substitution wrappers ─────────────────────────────────────────────────────
smt::Term do_substitute(const Ctx& ctx, const smt::Term& t,
                        const smt::UnorderedTermMap& subs) {
    if (subs.empty())
        return t;
    return ctx.solver->substitute(t, subs);
}

smt::Term do_substitute(const Ctx& ctx, const smt::Term& t, const smt::TermVec& from,
                        const smt::TermVec& to) {
    assert(from.size() == to.size());
    if (from.empty())
        return t;
    smt::UnorderedTermMap subs;
    for (size_t i = 0; i < from.size(); ++i)
        subs[from[i]] = to[i];
    return ctx.solver->substitute(t, subs);
}

smt::Term pairs2fla(const Ctx&                                          ctx,
                    const std::vector<std::pair<smt::Term, smt::Term>>& pairs) {
    smt::TermVec eqs;
    for (auto& [e, v] : pairs)
        eqs.push_back(ctx.solver->make_term(smt::Equal, e, v));
    return mk_and(ctx, eqs);
}
