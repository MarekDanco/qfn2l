#include "tests/test_support.h"
#include "qf_solver.h"
#include "stats.h"

static void test_integer_check_sat(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("sx", ctx.int_sort);
    smt::Term lb = ctx.solver->make_term(smt::Gt, x, ctx.make_int(4));

    expect_solver_sat(ctx, lb, "simple integer smt-switch/Z3 check was not SAT");

    smt::Term ub = ctx.solver->make_term(smt::Lt, x, ctx.make_int(0));
    smt::Term inconsistent = ctx.solver->make_term(smt::And, lb, ub);
    expect_solver_unsat(ctx, inconsistent,
                        "simple integer smt-switch/Z3 check was not UNSAT");
}

static void test_model_fix_linear_factor_solves_in_one_iteration() {
    STATS = Stats{};
    Ctx ctx = make_test_ctx();
    smt::Term x = ctx.solver->make_symbol("mfl_x", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("mfl_y", ctx.int_sort);
    smt::Term xy = ctx.solver->make_term(smt::Mult, x, y);
    smt::Term formula = ctx.solver->make_term(smt::Equal, xy, ctx.make_int(6));

    Options opts;
    opts.maxits = 1;
    opts.model_fix = true;
    QfSolver solver(ctx, opts, formula);
    std::optional<bool> res = solver.solve();
    expect_true(res && *res, "linear-factor model fix should solve x*y=6 in one iteration");
    expect_true(STATS.model_fix_attempts.value > 0, "expected model-fix attempts");
    expect_true(STATS.model_fix_successes.value == 1, "expected one model-fix success");
}

static void test_model_fix_is_opt_in() {
    STATS = Stats{};
    Ctx ctx = make_test_ctx();
    smt::Term x = ctx.solver->make_symbol("mfdis_x", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("mfdis_y", ctx.int_sort);
    smt::Term xy = ctx.solver->make_term(smt::Mult, x, y);
    smt::Term formula = ctx.solver->make_term(smt::Equal, xy, ctx.make_int(6));

    Options opts;
    opts.maxits = 1;
    QfSolver solver(ctx, opts, formula);
    std::optional<bool> res = solver.solve();
    expect_false(res && *res, "model fix should be disabled by default");
    expect_true(STATS.model_fix_attempts.value == 0, "disabled model-fix should not try repairs");
    expect_true(STATS.model_fix_successes.value == 0,
                "disabled model-fix should not report successes");
}

static void test_model_fix_exact_power_solves_in_one_iteration() {
    STATS = Stats{};
    Ctx ctx = make_test_ctx();
    smt::Term x = ctx.solver->make_symbol("mfp_x", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("mfp_y", ctx.int_sort);
    smt::Term x2 = ctx.solver->make_term(smt::Mult, x, x);
    smt::Term y3 = ctx.solver->make_term(smt::Mult, y, y, y);
    smt::Term prod = ctx.solver->make_term(smt::Mult, x2, y3);
    smt::Term formula = ctx.solver->make_term(smt::Equal, prod, ctx.make_int(27));

    Options opts;
    opts.maxits = 1;
    opts.model_fix = true;
    QfSolver solver(ctx, opts, formula);
    std::optional<bool> res = solver.solve();
    expect_true(res && *res, "exact-power model fix should solve x^2*y^3=27 in one iteration");
    expect_true(STATS.model_fix_attempts.value > 0, "expected model-fix attempts");
    expect_true(STATS.model_fix_successes.value == 1, "expected one model-fix success");
}

static void test_model_fix_one_adjustable_factor_solves_in_one_iteration() {
    STATS = Stats{};
    Ctx ctx = make_test_ctx();
    smt::Term x = ctx.solver->make_symbol("mfo_x", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("mfo_y", ctx.int_sort);
    smt::Term x2y = ctx.solver->make_term(smt::Mult, x, x, y);
    smt::Term product = ctx.solver->make_term(smt::Equal, x2y, ctx.make_int(12));
    smt::Term y_fixed = ctx.solver->make_term(smt::Equal, y, ctx.make_int(3));
    smt::Term formula = ctx.solver->make_term(smt::And, product, y_fixed);

    Options opts;
    opts.maxits = 1;
    opts.model_fix = true;
    opts.preprocess = true;
    QfSolver solver(ctx, opts, formula);
    std::optional<bool> res = solver.solve();
    expect_true(res && *res,
                "one-adjustable model fix should solve x^2*y=12, y=3 in one iteration");
    expect_true(STATS.model_fix_attempts.value > 0, "expected model-fix attempts");
    expect_true(STATS.model_fix_successes.value == 1, "expected one model-fix success");
}

int main() {
    {
        Ctx ctx = make_test_ctx();
        test_integer_check_sat(ctx);
    }
    test_model_fix_linear_factor_solves_in_one_iteration();
    test_model_fix_is_opt_in();
    test_model_fix_exact_power_solves_in_one_iteration();
    test_model_fix_one_adjustable_factor_solves_in_one_iteration();
    return 0;
}
