#include "projections.h"
#include <cassert>
#include <cmath>
#include <cstdlib>

bool is_negative_power(const smt::Term& root, int exp) {
    return (exp % 2 == 1) && is_neg_val(root);
}

// ── Power bound helpers ───────────────────────────────────────────────────────
std::pair<smt::Term, smt::Term> const_lb_pow(const Ctx& ctx, const smt::Term& x, int n,
                                             const smt::Term& x_val) {
    smt::Term vn = eval_pow(ctx, x_val, n);
    if (!is_neg_val(x_val)) {
        smt::Term cond = ctx.solver->make_term(smt::Ge, x, x_val);
        return {cond, vn};
    }
    if (n % 2 == 0) {
        smt::Term cond = ctx.solver->make_term(smt::Le, x, x_val);
        return {cond, vn};
    }
    // odd n, x_val < 0: condition is x_val <= x <= 0
    smt::Term cond = mk_and2(ctx, ctx.solver->make_term(smt::Le, x_val, x),
                             ctx.solver->make_term(smt::Le, x, ctx.ZERO));
    return {cond, vn};
}

std::pair<smt::Term, smt::Term> const_ub_pow(const Ctx& ctx, const smt::Term& x, int n,
                                             const smt::Term& x_val) {
    smt::Term vn = eval_pow(ctx, x_val, n);
    if (!is_neg_val(x_val)) {
        smt::Term cond = mk_and2(ctx, ctx.solver->make_term(smt::Le, ctx.ZERO, x),
                                 ctx.solver->make_term(smt::Le, x, x_val));
        return {cond, vn};
    }
    if (n % 2 == 0) {
        smt::Term cond = mk_and2(ctx, ctx.solver->make_term(smt::Le, x_val, x),
                                 ctx.solver->make_term(smt::Le, x, ctx.ZERO));
        return {cond, vn};
    }
    // odd n, x_val < 0
    smt::Term cond = ctx.solver->make_term(smt::Le, x, x_val);
    return {cond, vn};
}

std::pair<smt::Term, smt::Term> lin_lb_pow(const Ctx& ctx, const smt::Term& x, int n,
                                           const smt::Term& x_val) {
    smt::Term lin = mk_mul(ctx, {eval_pow(ctx, x_val, n - 1), x});
    if (!is_neg_val(x_val)) {
        smt::Term cond = ctx.solver->make_term(smt::Ge, x, x_val);
        return {cond, lin};
    }
    if (n % 2 == 0) {
        smt::Term cond = ctx.solver->make_term(smt::Le, x, x_val);
        return {cond, lin};
    }
    smt::Term cond = mk_and2(ctx, ctx.solver->make_term(smt::Le, x_val, x),
                             ctx.solver->make_term(smt::Le, x, ctx.ZERO));
    return {cond, lin};
}

std::pair<smt::Term, smt::Term> lin_ub_pow(const Ctx& ctx, const smt::Term& x, int n,
                                           const smt::Term& x_val) {
    smt::Term lin = mk_mul(ctx, {eval_pow(ctx, x_val, n - 1), x});
    if (!is_neg_val(x_val)) {
        smt::Term cond = mk_and2(ctx, ctx.solver->make_term(smt::Le, ctx.ZERO, x),
                                 ctx.solver->make_term(smt::Le, x, x_val));
        return {cond, lin};
    }
    if (n % 2 == 0) {
        smt::Term cond = mk_and2(ctx, ctx.solver->make_term(smt::Le, x_val, x),
                                 ctx.solver->make_term(smt::Le, x, ctx.ZERO));
        return {cond, lin};
    }
    smt::Term cond = ctx.solver->make_term(smt::Le, x, x_val);
    return {cond, lin};
}

// ── Combine lb/ub for two-variable products ───────────────────────────────────
static std::vector<std::pair<smt::Term, smt::Term>>
combine_lb_left(const Ctx& ctx, const smt::Term& x, int x_exp, const smt::Term& x_val,
                const smt::Term& y, int y_exp, const smt::Term& y_val) {
    bool neg_x = is_negative_power(x_val, x_exp);
    bool neg_y = is_negative_power(y_val, y_exp);
    auto [x_cond, x_bound] = (neg_y ? lin_ub_pow : lin_lb_pow)(ctx, x, x_exp, x_val);
    auto [y_cond, y_bound] =
        (neg_x ? const_ub_pow : const_lb_pow)(ctx, y, y_exp, y_val);
    smt::Term cond = mk_and2(ctx, x_cond, y_cond);
    smt::Term bound = mk_mul(ctx, {x_bound, y_bound});
    return {{cond, bound}};
}

std::vector<std::pair<smt::Term, smt::Term>>
combine_lb(const Ctx& ctx, const smt::Term& x, int x_exp, const smt::Term& x_val,
           const smt::Term& y, int y_exp, const smt::Term& y_val) {
    auto a = combine_lb_left(ctx, x, x_exp, x_val, y, y_exp, y_val);
    auto b = combine_lb_left(ctx, y, y_exp, y_val, x, x_exp, x_val);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static std::vector<std::pair<smt::Term, smt::Term>>
combine_ub_left(const Ctx& ctx, const smt::Term& x, int x_exp, const smt::Term& x_val,
                const smt::Term& y, int y_exp, const smt::Term& y_val) {
    bool neg_x = is_negative_power(x_val, x_exp);
    bool neg_y = is_negative_power(y_val, y_exp);
    auto [x_cond, x_bound] = (neg_y ? lin_lb_pow : lin_ub_pow)(ctx, x, x_exp, x_val);
    auto [y_cond, y_bound] =
        (neg_x ? const_lb_pow : const_ub_pow)(ctx, y, y_exp, y_val);
    smt::Term cond = mk_and2(ctx, x_cond, y_cond);
    smt::Term bound = mk_mul(ctx, {x_bound, y_bound});
    return {{cond, bound}};
}

std::vector<std::pair<smt::Term, smt::Term>>
combine_ub(const Ctx& ctx, const smt::Term& x, int x_exp, const smt::Term& x_val,
           const smt::Term& y, int y_exp, const smt::Term& y_val) {
    auto a = combine_ub_left(ctx, x, x_exp, x_val, y, y_exp, y_val);
    auto b = combine_ub_left(ctx, y, y_exp, y_val, x, x_exp, x_val);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

smt::Term triple_to_axiom(const Ctx& ctx, const smt::Term& condition,
                          const smt::Term& lhs, const smt::Term& rhs) {
    return mk_implies(ctx, condition, ctx.solver->make_term(smt::Le, lhs, rhs));
}

// ── mod_ax_mul ────────────────────────────────────────────────────────────────
// Modular product cuts are used when the current LIA model gives a pure product
// a residue that is incompatible with the residues of its factors. The generic
// cut is one-way; the special cases below are exact equivalences.
static bool is_prime_modulus(const unsigned int k) {
    if (k < 2)
        return false;
    for (unsigned int d = 2; d <= k / d; ++d)
        if (k % d == 0)
            return false;
    return true;
}

static smt::Term mod_eq(const Ctx& ctx, const smt::Term& term,
                        const unsigned int modulus, const int64_t residue) {
    const smt::Term kz = ctx.make_int(modulus);
    return ctx.solver->make_term(smt::Equal,
                                 ctx.solver->make_term(smt::Mod, term, kz),
                                 ctx.make_int(residue));
}

// Compute base_residue^exp modulo modulus. The exponents come from MulSplit
// groups and are always positive.
static int64_t pow_residue(const int64_t base_residue, const int exp,
                           const unsigned int modulus) {
    assert(exp > 0);
    int64_t rv = 1;
    const int64_t signed_modulus = static_cast<int64_t>(modulus);
    for (int e = 0; e < exp; ++e)
        rv = (rv * base_residue) % signed_modulus;
    return rv;
}

// Compute the residue of the concrete product represented by factors under the
// current model values stored in the third tuple component.
static int64_t product_residue(
    const Ctx& ctx,
    const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
    const unsigned int modulus) {
    int64_t residue = 1;
    const int64_t signed_modulus = static_cast<int64_t>(modulus);
    for (const auto& [_root, exp, val] : factors) {
        const int64_t rv = term_mod_int(ctx, val, modulus);
        residue = (residue * pow_residue(rv, exp, modulus)) % signed_modulus;
    }
    return residue;
}

// Over a prime modulus, a product is zero iff at least one factor is zero. This
// is stronger than the generic implication and is still linear after abstraction.
static smt::Term prime_zero_product_axiom(
    const Ctx& ctx,
    const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
    const smt::Term& pure_has_zero_residue, const unsigned int modulus) {
    smt::TermVec zero_factors;
    for (const auto& [root, _exp, _val] : factors)
        zero_factors.push_back(mod_eq(ctx, root, modulus, 0));
    return ctx.solver->make_term(smt::Equal, pure_has_zero_residue,
                                 mk_or(ctx, zero_factors));
}

// Modulo 2, a product is odd iff every factor is odd. Exponents do not need
// separate handling here because n^k is odd iff n is odd for k > 0.
static smt::Term odd_product_axiom(
    const Ctx& ctx,
    const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
    const smt::Term& pure_is_odd) {
    smt::TermVec odd_factors;
    for (const auto& [root, _exp, _val] : factors)
        odd_factors.push_back(mod_eq(ctx, root, 2, 1));
    return ctx.solver->make_term(smt::Equal, pure_is_odd, mk_and(ctx, odd_factors));
}

// Return an exact modular equivalence when the residue admits one cheaply.
// Otherwise return an empty Term so the caller can use the generic implication.
static smt::Term special_product_mod_axiom(
    const Ctx& ctx,
    const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
    const smt::Term& pure_has_expected_residue, const unsigned int modulus,
    const int64_t expected_residue) {
    if (expected_residue == 0 && is_prime_modulus(modulus))
        return prime_zero_product_axiom(ctx, factors, pure_has_expected_residue,
                                       modulus);
    if (modulus == 2 && expected_residue == 1)
        return odd_product_axiom(ctx, factors, pure_has_expected_residue);
    return {};
}

// Generic one-way congruence cut: if each factor keeps its current residue, then
// the product pure must have the product residue. If a factor residue is zero,
// that single condition is already enough to force product residue zero.
static smt::Term product_mod_implication(
    const Ctx& ctx,
    const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
    const smt::Term& pure_has_expected_residue, const unsigned int modulus) {
    smt::TermVec conditions;
    for (const auto& [root, _exp, val] : factors) {
        const int64_t fr = term_mod_int(ctx, val, modulus);
        const smt::Term cond = mod_eq(ctx, root, modulus, fr);
        if (fr == 0) {
            conditions = {cond};
            break;
        }
        conditions.push_back(cond);
    }
    return mk_implies(ctx, mk_and(ctx, conditions), pure_has_expected_residue);
}

// Find the first modulus whose product residue disagrees with the pure's model
// value and emit one modular axiom cutting off that model.
std::vector<smt::Term>
mod_ax_mul(const Ctx& ctx, const unsigned int max_modulus,
           const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
           const smt::Term& pure, const smt::Term& pure_val) {
    for (unsigned int k = 2; k <= max_modulus; ++k) {
        const int64_t expected_residue = product_residue(ctx, factors, k);
        const int64_t actual_residue = term_mod_int(ctx, pure_val, k);
        if (expected_residue == actual_residue)
            continue;

        const smt::Term pure_has_expected_residue = mod_eq(ctx, pure, k, expected_residue);
        const smt::Term special =
            special_product_mod_axiom(ctx, factors, pure_has_expected_residue, k,
                                      expected_residue);
        if (special)
            return {special};
        return {product_mod_implication(ctx, factors, pure_has_expected_residue, k)};
    }
    return {};
}

// ── project_y ────────────────────────────────────────────────────────────────
std::vector<std::tuple<smt::Term, smt::Term, smt::Term>>
project_y(const Ctx& ctx, const smt::Term& x, int x_exp, const smt::Term& y, int y_exp,
          const smt::Term& y_val, const smt::Term& pure_xm, const smt::Term& pure_res) {
    auto [lb_cond, lb_bound] = const_lb_pow(ctx, y, y_exp, y_val);
    auto [ub_cond, ub_bound] = const_ub_pow(ctx, y, y_exp, y_val);
    smt::Term zero = ctx.ZERO;

    smt::Term lb_xm = mk_mul(ctx, {lb_bound, pure_xm});
    smt::Term ub_xm = mk_mul(ctx, {ub_bound, pure_xm});

    if (x_exp % 2 == 0) {
        if (!is_negative_power(y_val, y_exp))
            return {{lb_cond, lb_xm, pure_res}, {ub_cond, pure_res, ub_xm}};
        else
            return {{ub_cond, pure_res, ub_xm}, {lb_cond, lb_xm, pure_res}};
    }
    // odd x_exp
    smt::Term xgt0 = ctx.solver->make_term(smt::Gt, x, zero);
    smt::Term xlt0 = ctx.solver->make_term(smt::Lt, x, zero);
    if (is_negative_power(y_val, y_exp))
        return {{mk_and2(ctx, xgt0, ub_cond), pure_res, ub_xm},
                {mk_and2(ctx, xlt0, ub_cond), ub_xm, pure_res}};
    else
        return {{mk_and2(ctx, xgt0, lb_cond), lb_xm, pure_res},
                {mk_and2(ctx, xlt0, lb_cond), pure_res, lb_xm}};
}
