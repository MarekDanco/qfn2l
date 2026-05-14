#pragma once
#include "utils.h"
#include <vector>

std::pair<Term, Term> const_lb_pow(const Ctx& ctx, const Term& x, int n,
                                   const Term& x_val);
std::pair<Term, Term> const_ub_pow(const Ctx& ctx, const Term& x, int n,
                                   const Term& x_val);
std::pair<Term, Term> lin_lb_pow(const Ctx& ctx, const Term& x, int n,
                                 const Term& x_val);
std::pair<Term, Term> lin_ub_pow(const Ctx& ctx, const Term& x, int n,
                                 const Term& x_val);

std::vector<std::pair<Term, Term>>
combine_lb(const Ctx& ctx, const Term& x, int x_exp, const Term& x_val,
           const Term& y, int y_exp, const Term& y_val);

std::vector<std::pair<Term, Term>>
combine_ub(const Ctx& ctx, const Term& x, int x_exp, const Term& x_val,
           const Term& y, int y_exp, const Term& y_val);

Term triple_to_axiom(const Ctx& ctx, const Term& condition,
                     const Term& lhs, const Term& rhs);

std::vector<Term>
mod_ax_mul(const Ctx& ctx, int max_modulus,
           const std::vector<std::tuple<Term, int, Term>>& factors,
           const Term& pure, const Term& pure_val);

std::vector<std::tuple<Term, Term, Term>>
project_y(const Ctx& ctx, const Term& x, int x_exp,
          const Term& y, int y_exp, const Term& y_val,
          const Term& pure_xm, const Term& pure_res);

bool is_negative_power(const Term& root, int exp);
