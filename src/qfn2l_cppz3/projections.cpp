#include "projections.h"
#include <cassert>
#include <cmath>
#include <cstdlib>

bool is_negative_power(const Term& root, int exp) {
    return (exp % 2 == 1) && is_neg_val(root);
}

// ── Power bound helpers ───────────────────────────────────────────────────────
std::pair<Term, Term> const_lb_pow(const Ctx& ctx, const Term& x, int n,
                                   const Term& x_val) {
    Term vn = eval_pow(ctx, x_val, n);
    if (!is_neg_val(x_val)) return {(x >= x_val), vn};
    if (n % 2 == 0) return {(x <= x_val), vn};
    // odd n, x_val < 0: condition is x_val <= x <= 0
    return {mk_and2(ctx, (x_val <= x), (x <= ctx.ZERO)), vn};
}

std::pair<Term, Term> const_ub_pow(const Ctx& ctx, const Term& x, int n,
                                   const Term& x_val) {
    Term vn = eval_pow(ctx, x_val, n);
    if (!is_neg_val(x_val))
        return {mk_and2(ctx, (ctx.ZERO <= x), (x <= x_val)), vn};
    if (n % 2 == 0)
        return {mk_and2(ctx, (x_val <= x), (x <= ctx.ZERO)), vn};
    return {(x <= x_val), vn};
}

std::pair<Term, Term> lin_lb_pow(const Ctx& ctx, const Term& x, int n,
                                 const Term& x_val) {
    Term lin = mk_mul(ctx, {eval_pow(ctx, x_val, n - 1), x});
    if (!is_neg_val(x_val)) return {(x >= x_val), lin};
    if (n % 2 == 0) return {(x <= x_val), lin};
    return {mk_and2(ctx, (x_val <= x), (x <= ctx.ZERO)), lin};
}

std::pair<Term, Term> lin_ub_pow(const Ctx& ctx, const Term& x, int n,
                                 const Term& x_val) {
    Term lin = mk_mul(ctx, {eval_pow(ctx, x_val, n - 1), x});
    if (!is_neg_val(x_val))
        return {mk_and2(ctx, (ctx.ZERO <= x), (x <= x_val)), lin};
    if (n % 2 == 0)
        return {mk_and2(ctx, (x_val <= x), (x <= ctx.ZERO)), lin};
    return {(x <= x_val), lin};
}

// ── Combine lb/ub for two-variable products ───────────────────────────────────
static std::vector<std::pair<Term, Term>>
combine_lb_left(const Ctx& ctx, const Term& x, int x_exp, const Term& x_val,
                const Term& y, int y_exp, const Term& y_val) {
    bool neg_x = is_negative_power(x_val, x_exp);
    bool neg_y = is_negative_power(y_val, y_exp);
    auto [x_cond, x_bound] = (neg_y ? lin_ub_pow : lin_lb_pow)(ctx, x, x_exp, x_val);
    auto [y_cond, y_bound] = (neg_x ? const_ub_pow : const_lb_pow)(ctx, y, y_exp, y_val);
    return {{mk_and2(ctx, x_cond, y_cond), mk_mul(ctx, {x_bound, y_bound})}};
}

std::vector<std::pair<Term, Term>>
combine_lb(const Ctx& ctx, const Term& x, int x_exp, const Term& x_val,
           const Term& y, int y_exp, const Term& y_val) {
    auto a = combine_lb_left(ctx, x, x_exp, x_val, y, y_exp, y_val);
    auto b = combine_lb_left(ctx, y, y_exp, y_val, x, x_exp, x_val);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static std::vector<std::pair<Term, Term>>
combine_ub_left(const Ctx& ctx, const Term& x, int x_exp, const Term& x_val,
                const Term& y, int y_exp, const Term& y_val) {
    bool neg_x = is_negative_power(x_val, x_exp);
    bool neg_y = is_negative_power(y_val, y_exp);
    auto [x_cond, x_bound] = (neg_y ? lin_lb_pow : lin_ub_pow)(ctx, x, x_exp, x_val);
    auto [y_cond, y_bound] = (neg_x ? const_lb_pow : const_ub_pow)(ctx, y, y_exp, y_val);
    return {{mk_and2(ctx, x_cond, y_cond), mk_mul(ctx, {x_bound, y_bound})}};
}

std::vector<std::pair<Term, Term>>
combine_ub(const Ctx& ctx, const Term& x, int x_exp, const Term& x_val,
           const Term& y, int y_exp, const Term& y_val) {
    auto a = combine_ub_left(ctx, x, x_exp, x_val, y, y_exp, y_val);
    auto b = combine_ub_left(ctx, y, y_exp, y_val, x, x_exp, x_val);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Term triple_to_axiom(const Ctx& ctx, const Term& condition,
                     const Term& lhs, const Term& rhs) {
    return mk_implies(ctx, condition, (lhs <= rhs));
}

// ── mod_ax_mul ────────────────────────────────────────────────────────────────
std::vector<Term>
mod_ax_mul(const Ctx& ctx, int max_modulus,
           const std::vector<std::tuple<Term, int, Term>>& factors,
           const Term& pure, const Term& pure_val) {
    for (int k = 2; k <= max_modulus; ++k) {
        int64_t expected_residue = 1;
        for (auto& [root, exp, val] : factors) {
            int64_t rv  = ((term_mod_int(ctx, val, k)) + k) % k;
            int64_t pwr = 1;
            for (int e = 0; e < exp; ++e) pwr = (pwr * rv) % k;
            expected_residue = (expected_residue * pwr) % k;
        }
        int64_t actual_residue = ((term_mod_int(ctx, pure_val, k)) + k) % k;
        if (expected_residue == actual_residue) continue;

        Term kz = ctx.make_int(k);
        TermVec conditions;
        for (auto& [root, _exp, val] : factors) {
            int64_t fr   = ((term_mod_int(ctx, val, k)) + k) % k;
            Term cond = (z3::mod(root, kz) == ctx.make_int(fr));
            if (fr == 0) { conditions = {cond}; break; }
            conditions.push_back(cond);
        }
        Term ax = mk_implies(ctx, mk_and(ctx, conditions),
                             (z3::mod(pure, kz) == ctx.make_int(expected_residue)));
        return {ax};
    }
    return {};
}

// ── project_y ────────────────────────────────────────────────────────────────
std::vector<std::tuple<Term, Term, Term>>
project_y(const Ctx& ctx, const Term& x, int x_exp,
          const Term& y, int y_exp, const Term& y_val,
          const Term& pure_xm, const Term& pure_res) {
    auto [lb_cond, lb_bound] = const_lb_pow(ctx, y, y_exp, y_val);
    auto [ub_cond, ub_bound] = const_ub_pow(ctx, y, y_exp, y_val);
    Term zero = ctx.ZERO;

    Term lb_xm = mk_mul(ctx, {lb_bound, pure_xm});
    Term ub_xm = mk_mul(ctx, {ub_bound, pure_xm});

    if (x_exp % 2 == 0) {
        if (!is_negative_power(y_val, y_exp))
            return {{lb_cond, lb_xm, pure_res}, {ub_cond, pure_res, ub_xm}};
        else
            return {{ub_cond, pure_res, ub_xm}, {lb_cond, lb_xm, pure_res}};
    }
    // odd x_exp
    Term xgt0 = (x > zero);
    Term xlt0 = (x < zero);
    if (is_negative_power(y_val, y_exp))
        return {{mk_and2(ctx, xgt0, ub_cond), pure_res, ub_xm},
                {mk_and2(ctx, xlt0, ub_cond), ub_xm, pure_res}};
    else
        return {{mk_and2(ctx, xgt0, lb_cond), lb_xm, pure_res},
                {mk_and2(ctx, xlt0, lb_cond), pure_res, lb_xm}};
}
