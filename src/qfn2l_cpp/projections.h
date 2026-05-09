#pragma once
#include "utils.h"
#include <vector>

// All functions return (condition, bound) pairs or axiom lists.
// Condition => pure >= bound  (for lb)  or  pure <= bound  (for ub).

std::pair<smt::Term, smt::Term>
const_lb_pow(const Ctx& ctx, const smt::Term& x, int n, const smt::Term& x_val);

std::pair<smt::Term, smt::Term>
const_ub_pow(const Ctx& ctx, const smt::Term& x, int n, const smt::Term& x_val);

std::pair<smt::Term, smt::Term>
lin_lb_pow(const Ctx& ctx, const smt::Term& x, int n, const smt::Term& x_val);

std::pair<smt::Term, smt::Term>
lin_ub_pow(const Ctx& ctx, const smt::Term& x, int n, const smt::Term& x_val);

// Returns (condition, lower_bound) pairs for x^xexp * y^yexp,
// linearizing first x then y.
std::vector<std::pair<smt::Term, smt::Term>>
combine_lb(const Ctx& ctx,
           const smt::Term& x, int x_exp, const smt::Term& x_val,
           const smt::Term& y, int y_exp, const smt::Term& y_val);

std::vector<std::pair<smt::Term, smt::Term>>
combine_ub(const Ctx& ctx,
           const smt::Term& x, int x_exp, const smt::Term& x_val,
           const smt::Term& y, int y_exp, const smt::Term& y_val);

// Convert (condition, lhs, rhs) -> Implies(condition, lhs <= rhs).
smt::Term triple_to_axiom(const Ctx& ctx,
                           const smt::Term& condition,
                           const smt::Term& lhs,
                           const smt::Term& rhs);

// Modular congruence axiom for a product of powers.
// factors = [(variable, exponent, current_value), ...]
std::vector<smt::Term>
mod_ax_mul(const Ctx& ctx, int max_modulus,
           const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
           const smt::Term& pure, const smt::Term& pure_val);

// Bound axioms for x^x_exp * y^y_exp when y is fixed.
// Returns (condition, lhs, rhs) triples.
std::vector<std::tuple<smt::Term, smt::Term, smt::Term>>
project_y(const Ctx& ctx,
          const smt::Term& x, int x_exp,
          const smt::Term& y, int y_exp, const smt::Term& y_val,
          const smt::Term& pure_xm, const smt::Term& pure_res);

bool is_negative_power(const smt::Term& root, int exp);
