#include "utils.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <regex>
#include <stdexcept>
#include <vector>

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
// Member declaration order must match struct layout so zctx is fully constructed
// before solver/sorts/constants.
Ctx::Ctx()
    : zctx(),
      solver(zctx),
      int_sort(zctx.int_sort()),
      bool_sort(zctx.bool_sort()),
      ZERO(zctx.int_val(int64_t(0))),
      ONE(zctx.int_val(int64_t(1))),
      MIN_ONE(zctx.int_val(int64_t(-1))),
      TRUE_T(zctx.bool_val(true)),
      FALSE_T(zctx.bool_val(false)) {}

Term Ctx::make_int(int64_t n) const { return zctx.int_val(n); }

Term Ctx::make_int_str(const std::string& s) const {
    return cpp_int_to_term(*this, parse_cpp_int(s));
}

Term Ctx::fresh_symbol(const z3::sort& sort, const std::string& prefix) const {
    int id = _fresh_ctr.fetch_add(1);
    std::string name = prefix + "_" + std::to_string(id);
    return zctx.constant(name.c_str(), sort);
}

Term Ctx::get_value(const Term& t) const {
    return solver.get_model().eval(t, /*model_completion=*/true);
}

// ── Numeral helpers ───────────────────────────────────────────────────────────
cpp_int term_to_cpp_int(const Term& t) {
    std::string s;
    if (t.is_numeral(s))
        return parse_cpp_int(s);
    return parse_cpp_int(t.to_string());
}

Term cpp_int_to_term(const Ctx& ctx, const cpp_int& v) {
    // Z3_mk_numeral accepts negative decimal strings for integer sorts.
    std::string s = v.str(); // e.g. "42" or "-42"
    return Term(ctx.zctx, Z3_mk_numeral(ctx.zctx, s.c_str(), ctx.int_sort));
}

int64_t cpp_int_to_int64(const cpp_int& v) {
    static const cpp_int mn = std::numeric_limits<int64_t>::min();
    static const cpp_int mx = std::numeric_limits<int64_t>::max();
    if (v < mn || v > mx)
        throw std::out_of_range("integer does not fit int64_t");
    return v.convert_to<int64_t>();
}

int64_t term_to_int64(const Term& t) { return cpp_int_to_int64(term_to_cpp_int(t)); }

int64_t term_mod_int(const Ctx&, const Term& val, int64_t k) {
    cpp_int v = term_to_cpp_int(val);
    cpp_int kk = k;
    cpp_int r  = v % kk;
    if (r < 0) r += kk;
    return cpp_int_to_int64(r);
}

bool is_value(const Term& t) {
    return t.is_numeral() || t.is_true() || t.is_false();
}
bool is_int_value(const Term& t) {
    return t.is_numeral() && t.get_sort().is_int();
}
bool is_symbol(const Term& t) { return is_symbolic_const(t); }
bool is_symbolic_const(const Term& t) {
    return t.is_app() && t.num_args() == 0 &&
           t.decl().decl_kind() == Z3_OP_UNINTERPRETED;
}

// ── Op-code predicates ────────────────────────────────────────────────────────
bool is_app(const Term& t) { return t.is_app(); }
bool is_app_of(const Term& t, Z3_decl_kind k) {
    return t.is_app() && t.decl().decl_kind() == k;
}

bool is_mul(const Term& t)      { return is_app_of(t, Z3_OP_MUL); }
bool is_add(const Term& t)      { return is_app_of(t, Z3_OP_ADD); }
bool is_sub(const Term& t)      { return is_app_of(t, Z3_OP_SUB); }
bool is_mod(const Term& t)      { return is_app_of(t, Z3_OP_MOD); }
bool is_idiv(const Term& t)     { return is_app_of(t, Z3_OP_IDIV); }
bool is_and(const Term& t)      { return is_app_of(t, Z3_OP_AND); }
bool is_or(const Term& t)       { return is_app_of(t, Z3_OP_OR); }
bool is_not(const Term& t)      { return is_app_of(t, Z3_OP_NOT); }
bool is_eq(const Term& t)       { return is_app_of(t, Z3_OP_EQ); }
bool is_distinct(const Term& t) { return is_app_of(t, Z3_OP_DISTINCT); }
bool is_le(const Term& t)       { return is_app_of(t, Z3_OP_LE); }
bool is_lt(const Term& t)       { return is_app_of(t, Z3_OP_LT); }
bool is_ge(const Term& t)       { return is_app_of(t, Z3_OP_GE); }
bool is_gt(const Term& t)       { return is_app_of(t, Z3_OP_GT); }
bool is_implies(const Term& t)  { return is_app_of(t, Z3_OP_IMPLIES); }
bool is_ite(const Term& t)      { return is_app_of(t, Z3_OP_ITE); }
bool is_quantifier(const Term& t) { return t.is_quantifier(); }
bool is_forall(const Term& t) {
    return t.is_quantifier() && Z3_is_quantifier_forall(t.ctx(), t);
}
bool is_exists(const Term& t) {
    return t.is_quantifier() && !Z3_is_quantifier_forall(t.ctx(), t);
}

bool is_int(const Ctx& ctx, const Term& t)  { return z3::eq(t.get_sort(), ctx.int_sort); }
bool is_bool(const Ctx& ctx, const Term& t) { return z3::eq(t.get_sort(), ctx.bool_sort); }

bool is_int_atom(const Ctx& ctx, const Term& t) {
    if (is_le(t) || is_lt(t) || is_ge(t) || is_gt(t)) return true;
    if ((is_eq(t) || is_distinct(t)) && t.num_args() > 0)
        return t.arg(0).get_sort().is_int();
    return false;
}

bool is_non_linear(const Term& t) {
    if (!is_mul(t)) return false;
    int symbolic = 0;
    for (unsigned i = 0; i < t.num_args(); ++i)
        if (!t.arg(i).is_numeral())
            ++symbolic;
    return symbolic > 1;
}

bool is_nnf_connective(const Term& t) {
    return is_and(t) || is_or(t) || is_not(t);
}

// ── Value checks ─────────────────────────────────────────────────────────────
// z3 interns numerals by value, so identity comparison is correct.
bool is_true(const Ctx& ctx, const Term& t)    { return z3::eq(t, ctx.TRUE_T); }
bool is_false(const Ctx& ctx, const Term& t)   { return z3::eq(t, ctx.FALSE_T); }
bool is_zero(const Ctx& ctx, const Term& t)    { return t.is_numeral() && z3::eq(t, ctx.ZERO); }
bool is_one(const Ctx& ctx, const Term& t)     { return t.is_numeral() && z3::eq(t, ctx.ONE); }
bool is_min_one(const Ctx& ctx, const Term& t) { return t.is_numeral() && z3::eq(t, ctx.MIN_ONE); }
bool is_neg_val(const Term& t) {
    if (!t.is_numeral()) return false;
    return term_to_cpp_int(t) < 0;
}

// ── Term building ─────────────────────────────────────────────────────────────
Term mk_true(const Ctx& ctx)  { return ctx.TRUE_T; }
Term mk_false(const Ctx& ctx) { return ctx.FALSE_T; }

Term mk_not(const Ctx& ctx, const Term& a) {
    if (is_not(a)) return a.arg(0);
    if (is_true(ctx, a))  return ctx.FALSE_T;
    if (is_false(ctx, a)) return ctx.TRUE_T;
    return !a;
}

Term mk_and(const Ctx& ctx, const TermVec& args) {
    if (args.empty()) return ctx.TRUE_T;
    TermVec filtered;
    for (auto& a : args) {
        if (is_false(ctx, a)) return ctx.FALSE_T;
        if (!is_true(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty())   return ctx.TRUE_T;
    if (filtered.size() == 1) return filtered[0];
    z3::expr_vector v(ctx.zctx);
    for (auto& a : filtered) v.push_back(a);
    return z3::mk_and(v);
}

Term mk_or(const Ctx& ctx, const TermVec& args) {
    if (args.empty()) return ctx.FALSE_T;
    TermVec filtered;
    for (auto& a : args) {
        if (is_true(ctx, a)) return ctx.TRUE_T;
        if (!is_false(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty())   return ctx.FALSE_T;
    if (filtered.size() == 1) return filtered[0];
    z3::expr_vector v(ctx.zctx);
    for (auto& a : filtered) v.push_back(a);
    return z3::mk_or(v);
}

Term mk_and2(const Ctx& ctx, const Term& a, const Term& b) {
    return mk_and(ctx, {a, b});
}
Term mk_or2(const Ctx& ctx, const Term& a, const Term& b) {
    return mk_or(ctx, {a, b});
}
Term mk_implies(const Ctx& ctx, const Term& a, const Term& b) {
    return z3::implies(a, b);
}

Term mk_mul(const Ctx& ctx, const TermVec& args) {
    if (args.empty()) return ctx.ONE;
    TermVec filtered;
    for (auto& a : args) {
        if (is_zero(ctx, a)) return ctx.ZERO;
        if (!is_one(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty())    return ctx.ONE;
    if (filtered.size() == 1) return filtered[0];
    // Use C API for flat n-ary multiplication.
    std::vector<Z3_ast> za;
    za.reserve(filtered.size());
    for (auto& t : filtered) za.push_back(static_cast<Z3_ast>(t));
    return Term(ctx.zctx, Z3_mk_mul(ctx.zctx, za.size(), za.data()));
}

Term mk_add(const Ctx& ctx, const TermVec& args) {
    if (args.empty()) return ctx.ZERO;
    TermVec filtered;
    for (auto& a : args) {
        if (!is_zero(ctx, a)) filtered.push_back(a);
    }
    if (filtered.empty())    return ctx.ZERO;
    if (filtered.size() == 1) return filtered[0];
    std::vector<Z3_ast> za;
    za.reserve(filtered.size());
    for (auto& t : filtered) za.push_back(static_cast<Z3_ast>(t));
    return Term(ctx.zctx, Z3_mk_add(ctx.zctx, za.size(), za.data()));
}

Term eval_mul(const Ctx& ctx, const TermVec& args) {
    if (args.empty()) return ctx.ONE;
    for (auto& a : args) { assert(a.is_numeral()); (void)a; }
    if (args.size() == 1) return args[0];
    cpp_int r = 1;
    for (auto& a : args) r *= term_to_cpp_int(a);
    return cpp_int_to_term(ctx, r);
}

Term eval_sum(const Ctx& ctx, const TermVec& args) {
    if (args.empty()) return ctx.ZERO;
    for (auto& a : args) { assert(a.is_numeral()); (void)a; }
    if (args.size() == 1) return args[0];
    cpp_int r = 0;
    for (auto& a : args) r += term_to_cpp_int(a);
    return cpp_int_to_term(ctx, r);
}

Term eval_exp(const Ctx& ctx, const Term& x, int n) {
    assert(n >= 0);
    assert(x.is_numeral());
    if (n == 0) return ctx.ONE;
    cpp_int v = term_to_cpp_int(x);
    cpp_int r = 1;
    for (int i = 0; i < n; ++i) r *= v;
    return cpp_int_to_term(ctx, r);
}

Term mk_pow(const Ctx& ctx, const Term& x, int n) {
    assert(n >= 0);
    if (n == 0) return ctx.ONE;
    TermVec factors(n, x);
    return mk_mul(ctx, factors);
}

Term eval_pow(const Ctx& ctx, const Term& x, int n) {
    TermVec factors(n, x);
    return eval_mul(ctx, factors);
}

Term negate_numeral(const Ctx& ctx, const Term& n) {
    assert(n.is_numeral());
    return cpp_int_to_term(ctx, -term_to_cpp_int(n));
}

// ── Child access ──────────────────────────────────────────────────────────────
TermVec get_children(const Term& t) {
    TermVec result;
    if (t.is_app()) {
        result.reserve(t.num_args());
        for (unsigned i = 0; i < t.num_args(); ++i)
            result.push_back(t.arg(i));
    } else if (t.is_quantifier()) {
        result.push_back(t.body());
    }
    return result;
}

size_t num_children(const Term& t) {
    if (t.is_app()) return t.num_args();
    if (t.is_quantifier()) return 1;
    return 0;
}

Term get_child(const Term& t, size_t i) {
    if (t.is_app()) return t.arg(static_cast<unsigned>(i));
    if (t.is_quantifier() && i == 0) return t.body();
    assert(false && "get_child: index out of range");
    return t; // unreachable
}

Term rebuild(const Ctx& ctx, const Term& t, const TermVec& new_args) {
    assert(t.is_app());
    z3::func_decl f = t.decl();
    z3::expr_vector v(ctx.zctx);
    for (auto& a : new_args) v.push_back(a);
    return f(v);
}

// ── Variable collection ───────────────────────────────────────────────────────
TermVec get_vars(const Term& root) {
    TermSet vars, visited;
    std::vector<Term> stk = {root};
    while (!stk.empty()) {
        Term t = stk.back();
        stk.pop_back();
        if (!visited.insert(t).second) continue;
        if (is_symbolic_const(t)) vars.insert(t);
        for (unsigned i = 0; i < t.num_args(); ++i)
            stk.push_back(t.arg(i));
    }
    return TermVec(vars.begin(), vars.end());
}

// ── Substitution wrappers ─────────────────────────────────────────────────────
Term do_substitute(const Ctx& ctx, const Term& t, const TermMap& subs) {
    if (subs.empty()) return t;
    z3::expr_vector from(ctx.zctx), to(ctx.zctx);
    for (auto& [k, v] : subs) {
        from.push_back(k);
        to.push_back(v);
    }
    Term t_copy = t; // substitute is non-const in z3 API; copy is cheap
    return t_copy.substitute(from, to);
}

Term do_substitute(const Ctx& ctx, const Term& t, const TermVec& from,
                   const TermVec& to) {
    assert(from.size() == to.size());
    if (from.empty()) return t;
    TermMap subs;
    for (size_t i = 0; i < from.size(); ++i)
        subs.insert_or_assign(from[i], to[i]);
    return do_substitute(ctx, t, subs);
}

Term pairs2fla(const Ctx& ctx, const std::vector<std::pair<Term, Term>>& pairs) {
    TermVec eqs;
    for (auto& [e, v] : pairs)
        eqs.push_back(e == v);
    return mk_and(ctx, eqs);
}
