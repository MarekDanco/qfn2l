#pragma once
// Central context and low-level term utilities.
// All term-building helpers take Ctx by const ref so we have one solver.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>

#include "smt.h" // smt-switch main header

// ── Solver context ────────────────────────────────────────────────────────────
struct Ctx {
    smt::SmtSolver solver;
    smt::Sort int_sort;
    smt::Sort bool_sort;
    smt::Term ZERO, ONE, MIN_ONE;
    smt::Term TRUE_T, FALSE_T;

    // TODO: verify smt-switch factory header names for chosen backend.
    // Construct from a pre-created SmtSolver.
    explicit Ctx(smt::SmtSolver s);

    smt::Term make_int(int64_t n) const;
    // Create an integer term from a decimal string, including big integers.
    // Accepts "N", "-N", or SMT-LIB sexpr form "(- N)".
    smt::Term make_int_str(const std::string& s) const;
    // Create a fresh symbol with the given sort (unique name, globally unique).
    smt::Term fresh_symbol(const smt::Sort& sort,
                           const std::string& prefix = "_p") const;

  private:
    mutable std::atomic<int> _fresh_ctr{0};
};

// ── Numeral helpers ───────────────────────────────────────────────────────────
// Extract an arbitrary-precision integer value from a numeric constant term.
boost::multiprecision::cpp_int term_to_cpp_int(const smt::Term& t);
// Build an integer term from an arbitrary-precision value.
smt::Term cpp_int_to_term(const Ctx& ctx, const boost::multiprecision::cpp_int& v);
// Narrow an arbitrary-precision integer to int64_t. Throws on overflow.
int64_t cpp_int_to_int64(const boost::multiprecision::cpp_int& v);
// Extract integer value from a numeric constant term. Throws on overflow.
int64_t term_to_int64(const smt::Term& t);
// Compute (val % k) for a numeral term val and small integer k.
// Compute (val % k) for a numeral term val and small integer k via cpp_int.
// Result fits in int64_t (|result| < k).
int64_t term_mod_int(const Ctx& ctx, const smt::Term& val, int64_t k);

bool is_value(const smt::Term& t);          // concrete value (int or bool)
bool is_int_value(const smt::Term& t);      // concrete integer
bool is_symbol(const smt::Term& t);         // symbolic constant (SYMBOL kind)
bool is_symbolic_const(const smt::Term& t); // symbol that is not a numeral

// ── Op-code predicates ────────────────────────────────────────────────────────
bool is_app(const smt::Term& t);
bool is_app_of(const smt::Term& t, smt::PrimOp op);

bool is_mul(const smt::Term& t);
bool is_add(const smt::Term& t);
bool is_sub(const smt::Term& t);
bool is_mod(const smt::Term& t);
bool is_idiv(const smt::Term& t); // integer div
bool is_and(const smt::Term& t);
bool is_or(const smt::Term& t);
bool is_not(const smt::Term& t);
bool is_eq(const smt::Term& t);
bool is_distinct(const smt::Term& t);
bool is_le(const smt::Term& t);
bool is_lt(const smt::Term& t);
bool is_ge(const smt::Term& t);
bool is_gt(const smt::Term& t);
bool is_implies(const smt::Term& t);
bool is_ite(const smt::Term& t);
bool is_quantifier(const smt::Term& t);
bool is_forall(const smt::Term& t);
bool is_exists(const smt::Term& t);

// Sort checks
bool is_int(const Ctx& ctx, const smt::Term& t);
bool is_bool(const Ctx& ctx, const smt::Term& t);
bool is_int_atom(const Ctx& ctx, const smt::Term& t);
bool is_non_linear(const smt::Term& t);     // mul with 2+ symbolic children
bool is_nnf_connective(const smt::Term& t); // and / or / not

// ── Value checks ─────────────────────────────────────────────────────────────
bool is_true(const Ctx& ctx, const smt::Term& t);
bool is_false(const Ctx& ctx, const smt::Term& t);
bool is_zero(const Ctx& ctx, const smt::Term& t);
bool is_one(const Ctx& ctx, const smt::Term& t);
bool is_min_one(const Ctx& ctx, const smt::Term& t);
bool is_neg_val(const smt::Term& t); // value < 0

// ── Term building helpers ─────────────────────────────────────────────────────
smt::Term mk_true(const Ctx& ctx);
smt::Term mk_false(const Ctx& ctx);

smt::Term mk_not(const Ctx& ctx, const smt::Term& a);
smt::Term mk_and(const Ctx& ctx, const smt::TermVec& args);
smt::Term mk_or(const Ctx& ctx, const smt::TermVec& args);
smt::Term mk_and2(const Ctx& ctx, const smt::Term& a, const smt::Term& b);
smt::Term mk_or2(const Ctx& ctx, const smt::Term& a, const smt::Term& b);
smt::Term mk_implies(const Ctx& ctx, const smt::Term& a, const smt::Term& b);

smt::Term mk_mul(const Ctx& ctx, const smt::TermVec& args);
smt::Term mk_add(const Ctx& ctx, const smt::TermVec& args);

// Evaluate (constant-fold) a product / sum of numeral terms.
smt::Term eval_mul(const Ctx& ctx, const smt::TermVec& args);
smt::Term eval_sum(const Ctx& ctx, const smt::TermVec& args);
// Evaluate x^n (all numerals).
smt::Term eval_exp(const Ctx& ctx, const smt::Term& x, int n);
// Build x^n as a product term (symbolic x).
smt::Term mk_pow(const Ctx& ctx, const smt::Term& x, int n);
smt::Term eval_pow(const Ctx& ctx, const smt::Term& x, int n);

smt::Term negate_numeral(const Ctx& ctx, const smt::Term& n);

// ── Child access ──────────────────────────────────────────────────────────────
smt::TermVec get_children(const smt::Term& t);
size_t num_children(const smt::Term& t);
smt::Term get_child(const smt::Term& t, size_t i);

// Rebuild t with new children (same op).
smt::Term rebuild(const Ctx& ctx, const smt::Term& t, const smt::TermVec& new_args);

// ── Variable collection ───────────────────────────────────────────────────────
smt::TermVec get_vars(const smt::Term& t); // all SYMBOL-kind sub-terms

// ── Substitution wrapper ──────────────────────────────────────────────────────
smt::Term do_substitute(const Ctx& ctx, const smt::Term& t,
                        const smt::UnorderedTermMap& subs);
smt::Term do_substitute(const Ctx& ctx, const smt::Term& t, const smt::TermVec& from,
                        const smt::TermVec& to);

// pairs -> And(a == v, ...)
smt::Term pairs2fla(const Ctx& ctx,
                    const std::vector<std::pair<smt::Term, smt::Term>>& pairs);
