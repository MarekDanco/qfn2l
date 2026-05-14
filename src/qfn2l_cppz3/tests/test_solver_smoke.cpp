#include "tests/test_support.h"

static void test_integer_check_sat(Ctx& ctx) {
    Term x = ctx.fresh_symbol(ctx.int_sort, "sx");
    Term lb = (x > ctx.make_int(4));

    expect_solver_sat(ctx, lb, "simple integer z3 check was not SAT");

    Term ub = (x < ctx.make_int(0));
    Term inconsistent = mk_and2(ctx, lb, ub);
    expect_solver_unsat(ctx, inconsistent,
                        "simple integer z3 check was not UNSAT");
}

int main() {
    Ctx ctx = make_test_ctx();
    test_integer_check_sat(ctx);
    return 0;
}
