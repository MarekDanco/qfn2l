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
    bool neg_x             = is_negative_power(x_val, x_exp);
    bool neg_y             = is_negative_power(y_val, y_exp);
    auto [x_cond, x_bound] = (neg_y ? lin_ub_pow : lin_lb_pow)(ctx, x, x_exp, x_val);
    auto [y_cond, y_bound] =
        (neg_x ? const_ub_pow : const_lb_pow)(ctx, y, y_exp, y_val);
    smt::Term cond  = mk_and2(ctx, x_cond, y_cond);
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
    bool neg_x             = is_negative_power(x_val, x_exp);
    bool neg_y             = is_negative_power(y_val, y_exp);
    auto [x_cond, x_bound] = (neg_y ? lin_lb_pow : lin_ub_pow)(ctx, x, x_exp, x_val);
    auto [y_cond, y_bound] =
        (neg_x ? const_lb_pow : const_ub_pow)(ctx, y, y_exp, y_val);
    smt::Term cond  = mk_and2(ctx, x_cond, y_cond);
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
std::vector<smt::Term>
mod_ax_mul(const Ctx& ctx, int max_modulus,
           const std::vector<std::tuple<smt::Term, int, smt::Term>>& factors,
           const smt::Term& pure, const smt::Term& pure_val) {
    for (int k = 2; k <= max_modulus; ++k) {
        int64_t expected_residue = 1;
        for (auto& [root, exp, val] : factors) {
            int64_t rv  = ((term_mod_int(ctx, val, k)) + k) % k;
            int64_t pwr = 1;
            for (int e = 0; e < exp; ++e)
                pwr = (pwr * rv) % k;
            expected_residue = (expected_residue * pwr) % k;
        }
        int64_t actual_residue = ((term_mod_int(ctx, pure_val, k)) + k) % k;
        if (expected_residue == actual_residue)
            continue;

        smt::Term    kz = ctx.make_int(k);
        smt::TermVec conditions;
        for (auto& [root, _exp, val] : factors) {
            int64_t   fr   = ((term_mod_int(ctx, val, k)) + k) % k;
            smt::Term cond = ctx.solver->make_term(
                smt::Equal, ctx.solver->make_term(smt::Mod, root, kz),
                ctx.make_int(fr));
            if (fr == 0) {
                conditions = {cond};
                break;
            }
            conditions.push_back(cond);
        }
        smt::Term ax = mk_implies(
            ctx, mk_and(ctx, conditions),
            ctx.solver->make_term(smt::Equal, ctx.solver->make_term(smt::Mod, pure, kz),
                                  ctx.make_int(expected_residue)));
        return {ax};
    }
    return {};
}

// ── project_y ────────────────────────────────────────────────────────────────
std::vector<std::tuple<smt::Term, smt::Term, smt::Term>>
project_y(const Ctx& ctx, const smt::Term& x, int x_exp, const smt::Term& y, int y_exp,
          const smt::Term& y_val, const smt::Term& pure_xm, const smt::Term& pure_res) {
    auto [lb_cond, lb_bound] = const_lb_pow(ctx, y, y_exp, y_val);
    auto [ub_cond, ub_bound] = const_ub_pow(ctx, y, y_exp, y_val);
    smt::Term zero           = ctx.ZERO;

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
