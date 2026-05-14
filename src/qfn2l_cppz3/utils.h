#pragma once
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>
#include <z3++.h>

// ── Z3 type aliases ───────────────────────────────────────────────────────────
// These replace the smt-switch abstraction throughout the codebase.
using Term    = z3::expr;
using TermVec = std::vector<z3::expr>;

struct ExprHash {
    size_t operator()(const z3::expr& e) const noexcept { return e.hash(); }
};
// Use AST id (pointer identity) for equality — correct even for numerals because
// z3 interns all numeral ASTs by value.  This avoids the smt-switch bug where
// hash-collision caused pos9 == neg9.
struct ExprEq {
    bool operator()(const z3::expr& a, const z3::expr& b) const noexcept {
        return a.id() == b.id();
    }
};
using TermMap = std::unordered_map<Term, Term, ExprHash, ExprEq>;
using TermSet = std::unordered_set<Term, ExprHash, ExprEq>;

// ── Solver context ────────────────────────────────────────────────────────────
struct Ctx {
    // zctx is mutable because z3::context methods (int_val, constant, etc.) all
    // require non-const context& even when they are logically read-only, and we
    // pass Ctx as const Ctx& throughout.
    mutable z3::context zctx;
    mutable z3::solver  solver; // repopulated with each SAT model
    mutable z3::sort    int_sort;
    mutable z3::sort    bool_sort;
    mutable Term ZERO, ONE, MIN_ONE;
    mutable Term TRUE_T, FALSE_T;

    Ctx();

    Term make_int(int64_t n) const;
    // Build an integer term from a decimal string (including big integers and
    // SMT-LIB s-expression "(- N)" form).
    Term make_int_str(const std::string& s) const;
    // Create a fresh uninterpreted constant with a globally-unique name.
    Term fresh_symbol(const z3::sort& sort, const std::string& prefix = "_p") const;
    // Evaluate t under the current model (only valid when solver is SAT).
    Term get_value(const Term& t) const;

  private:
    mutable std::atomic<int> _fresh_ctr{0};
};

// ── Numeral helpers ───────────────────────────────────────────────────────────
boost::multiprecision::cpp_int term_to_cpp_int(const Term& t);
Term cpp_int_to_term(const Ctx& ctx, const boost::multiprecision::cpp_int& v);
int64_t cpp_int_to_int64(const boost::multiprecision::cpp_int& v);
int64_t term_to_int64(const Term& t);
// Compute (val % k) for a numeral term val and small integer k.
int64_t term_mod_int(const Ctx& ctx, const Term& val, int64_t k);

bool is_value(const Term& t);          // integer numeral or bool literal
bool is_int_value(const Term& t);      // integer numeral
bool is_symbol(const Term& t);         // uninterpreted constant
bool is_symbolic_const(const Term& t); // same as is_symbol

// ── Op-code predicates ────────────────────────────────────────────────────────
bool is_app(const Term& t);
bool is_app_of(const Term& t, Z3_decl_kind k);

bool is_mul(const Term& t);
bool is_add(const Term& t);
bool is_sub(const Term& t);
bool is_mod(const Term& t);
bool is_idiv(const Term& t);
bool is_and(const Term& t);
bool is_or(const Term& t);
bool is_not(const Term& t);
bool is_eq(const Term& t);
bool is_distinct(const Term& t);
bool is_le(const Term& t);
bool is_lt(const Term& t);
bool is_ge(const Term& t);
bool is_gt(const Term& t);
bool is_implies(const Term& t);
bool is_ite(const Term& t);
bool is_quantifier(const Term& t);
bool is_forall(const Term& t);
bool is_exists(const Term& t);

bool is_int(const Ctx& ctx, const Term& t);
bool is_bool(const Ctx& ctx, const Term& t);
bool is_int_atom(const Ctx& ctx, const Term& t);
bool is_non_linear(const Term& t);
bool is_nnf_connective(const Term& t);

// ── Value checks ─────────────────────────────────────────────────────────────
bool is_true(const Ctx& ctx, const Term& t);
bool is_false(const Ctx& ctx, const Term& t);
bool is_zero(const Ctx& ctx, const Term& t);
bool is_one(const Ctx& ctx, const Term& t);
bool is_min_one(const Ctx& ctx, const Term& t);
bool is_neg_val(const Term& t); // numeral < 0

// ── Term building helpers ─────────────────────────────────────────────────────
Term mk_true(const Ctx& ctx);
Term mk_false(const Ctx& ctx);

Term mk_not(const Ctx& ctx, const Term& a);
Term mk_and(const Ctx& ctx, const TermVec& args);
Term mk_or(const Ctx& ctx, const TermVec& args);
Term mk_and2(const Ctx& ctx, const Term& a, const Term& b);
Term mk_or2(const Ctx& ctx, const Term& a, const Term& b);
Term mk_implies(const Ctx& ctx, const Term& a, const Term& b);

Term mk_mul(const Ctx& ctx, const TermVec& args);
Term mk_add(const Ctx& ctx, const TermVec& args);

Term eval_mul(const Ctx& ctx, const TermVec& args);
Term eval_sum(const Ctx& ctx, const TermVec& args);
Term eval_exp(const Ctx& ctx, const Term& x, int n);
Term mk_pow(const Ctx& ctx, const Term& x, int n);
Term eval_pow(const Ctx& ctx, const Term& x, int n);

Term negate_numeral(const Ctx& ctx, const Term& n);

// ── Child access ──────────────────────────────────────────────────────────────
// Returns application arguments.  For quantifiers, returns {body()}.
TermVec get_children(const Term& t);
size_t  num_children(const Term& t);
Term    get_child(const Term& t, size_t i);

// Rebuild t (must be an application) with replacement children.
Term rebuild(const Ctx& ctx, const Term& t, const TermVec& new_args);

// ── Variable collection ───────────────────────────────────────────────────────
TermVec get_vars(const Term& t); // all uninterpreted constants

// ── Substitution wrappers ─────────────────────────────────────────────────────
Term do_substitute(const Ctx& ctx, const Term& t, const TermMap& subs);
Term do_substitute(const Ctx& ctx, const Term& t, const TermVec& from,
                   const TermVec& to);

// pairs -> And(a == v, ...)
Term pairs2fla(const Ctx& ctx, const std::vector<std::pair<Term, Term>>& pairs);
