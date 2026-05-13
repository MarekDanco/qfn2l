#include "tests/test_support.h"
#include "projections.h"

using boost::multiprecision::cpp_int;

static void expect_bound_valid_at(Ctx& ctx, const smt::Term& condition,
                                  const smt::Term& lhs, const smt::Term& rhs,
                                  const smt::UnorderedTermMap& subst,
                                  const std::string& message) {
    smt::Term implication =
        triple_to_axiom(ctx, condition, lhs, rhs);
    smt::Term instantiated =
        do_substitute(ctx, implication, subst);
    expect_solver_valid(ctx, instantiated, message);
}

static void test_negative_power_detection(Ctx& ctx) {
    smt::Term neg = ctx.make_int(-3);
    smt::Term pos = ctx.make_int(3);

    expect_true(is_negative_power(neg, 3), "negative odd power not detected");
    expect_false(is_negative_power(neg, 2), "negative even power marked negative");
    expect_false(is_negative_power(pos, 3), "positive odd power marked negative");
}

static void test_modular_axiom_generation(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("mx", ctx.int_sort);
    smt::Term pure = ctx.solver->make_symbol("mp", ctx.int_sort);

    auto none = mod_ax_mul(ctx, 11, {{x, 2, ctx.make_int(4)}}, pure, ctx.make_int(16));
    expect_true(none.empty(), "matching residue generated an unnecessary axiom");

    auto axs = mod_ax_mul(ctx, 11, {{x, 2, ctx.make_int(4)}}, pure, ctx.make_int(15));
    expect_true(axs.size() == 1, "mismatching residue did not generate one axiom");
    expect_true(is_implies(axs[0]), "modular axiom should be an implication");

    smt::UnorderedTermMap bad_residue;
    bad_residue[x] = ctx.make_int(4);
    bad_residue[pure] = ctx.make_int(15);
    expect_solver_unsat(ctx, do_substitute(ctx, axs[0], bad_residue),
                        "modular axiom did not reject an inconsistent pure residue");
}

static void test_power_bounds_on_sample_points(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("bx", ctx.int_sort);
    smt::Term x_val = ctx.make_int(-3);

    auto [clb, const_lb] = const_lb_pow(ctx, x, 3, x_val);
    auto [cub, const_ub] = const_ub_pow(ctx, x, 3, x_val);
    auto [llb, lin_lb] = lin_lb_pow(ctx, x, 3, x_val);
    auto [lub, lin_ub] = lin_ub_pow(ctx, x, 3, x_val);

    for (int xv : {-5, -3, -2, 0, 2}) {
        smt::UnorderedTermMap subst;
        subst[x] = ctx.make_int(xv);
        smt::Term actual = eval_pow(ctx, ctx.make_int(xv), 3);

        expect_bound_valid_at(ctx, clb, const_lb, actual, subst,
                              "const lower power bound failed");
        expect_bound_valid_at(ctx, cub, actual, const_ub, subst,
                              "const upper power bound failed");
        expect_bound_valid_at(ctx, llb, lin_lb, actual, subst,
                              "linear lower power bound failed");
        expect_bound_valid_at(ctx, lub, actual, lin_ub, subst,
                              "linear upper power bound failed");
    }
}

static void test_project_y_bounds_on_sample_points(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("px", ctx.int_sort);
    smt::Term pure_xm = ctx.solver->make_symbol("pxm", ctx.int_sort);
    smt::Term pure_res = ctx.solver->make_symbol("pres", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("py", ctx.int_sort);
    smt::Term y_val = ctx.make_int(3);

    auto triples = project_y(ctx, x, 1, y, 2, y_val, pure_xm, pure_res);
    expect_true(triples.size() == 2, "project_y should produce two bounds here");

    for (int xv : {-2, -1, 0, 1, 2}) {
        smt::UnorderedTermMap subst;
        subst[x] = ctx.make_int(xv);
        subst[y] = y_val;
        subst[pure_xm] = ctx.make_int(xv);
        subst[pure_res] = ctx.make_int(9 * xv);

        for (const auto& [condition, lhs, rhs] : triples)
            expect_bound_valid_at(ctx, condition, lhs, rhs, subst,
                                  "project_y generated an invalid bound");
    }
}

int main() {
    Ctx ctx = make_test_ctx();
    test_negative_power_detection(ctx);
    test_modular_axiom_generation(ctx);
    test_power_bounds_on_sample_points(ctx);
    test_project_y_bounds_on_sample_points(ctx);
    return 0;
}
