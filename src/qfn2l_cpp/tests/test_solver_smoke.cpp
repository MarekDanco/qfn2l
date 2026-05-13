#include "tests/test_support.h"

static void test_integer_check_sat(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("sx", ctx.int_sort);
    smt::Term lb = ctx.solver->make_term(smt::Gt, x, ctx.make_int(4));

    expect_solver_sat(ctx, lb, "simple integer smt-switch/Z3 check was not SAT");

    smt::Term ub = ctx.solver->make_term(smt::Lt, x, ctx.make_int(0));
    smt::Term inconsistent = ctx.solver->make_term(smt::And, lb, ub);
    expect_solver_unsat(ctx, inconsistent,
                        "simple integer smt-switch/Z3 check was not UNSAT");
}

int main() {
    Ctx ctx = make_test_ctx();
    test_integer_check_sat(ctx);
    return 0;
}
