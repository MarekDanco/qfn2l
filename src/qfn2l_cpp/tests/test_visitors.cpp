#include "tests/test_support.h"
#include "visitors.h"

static int count_child(const smt::Term& t, const smt::Term& child) {
    int count = 0;
    for (auto it = t->begin(); it != t->end(); ++it)
        if (*it == child)
            ++count;
    return count;
}

static void test_flatten_mul(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("x", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("y", ctx.int_sort);
    smt::Term z = ctx.solver->make_symbol("z", ctx.int_sort);

    smt::Term yz = ctx.solver->make_term(smt::Mult, y, z);
    smt::Term nested = ctx.solver->make_term(smt::Mult, x, yz);
    smt::Term flat = FlattenMul(ctx)(nested);

    expect_true(is_mul(flat), "flattened term is not multiplication");
    expect_true(num_children(flat) == 3, "nested multiplication was not flattened");
    expect_true(count_child(flat, x) == 1, "x missing or duplicated after flattening");
    expect_true(count_child(flat, y) == 1, "y missing or duplicated after flattening");
    expect_true(count_child(flat, z) == 1, "z missing or duplicated after flattening");
}

static void test_simple_simplify_arithmetic(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("sx", ctx.int_sort);

    smt::Term sum = ctx.solver->make_term(
        smt::Plus, {ctx.ZERO, ctx.make_int(2),
                    ctx.solver->make_term(smt::Plus, ctx.make_int(3), x)});
    smt::Term simplified_sum = SimpleSimplify(ctx)(sum);

    expect_true(is_add(simplified_sum), "simplified sum should still contain x");
    expect_true(num_children(simplified_sum) == 2,
                "simplified sum should contain one coefficient and one symbol");

    bool saw_five = false;
    bool saw_x = false;
    for (auto it = simplified_sum->begin(); it != simplified_sum->end(); ++it) {
        if (*it == x)
            saw_x = true;
        if ((*it)->is_value() && term_to_cpp_int(*it) == 5)
            saw_five = true;
    }
    expect_true(saw_x, "simplified sum lost symbolic child");
    expect_true(saw_five, "simplified sum did not fold numeric children");

    smt::Term product = ctx.solver->make_term(smt::Mult, {ctx.ONE, ctx.ZERO, x});
    expect_true(SimpleSimplify(ctx)(product) == ctx.ZERO,
                "multiplication by zero did not simplify to zero");
}

static void test_simple_propagate(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("px", ctx.int_sort);
    smt::Term eq = ctx.solver->make_term(smt::Equal, x, ctx.make_int(2));
    smt::Term gt = ctx.solver->make_term(smt::Gt, x, ctx.ZERO);
    smt::Term formula = mk_and(ctx, {eq, gt});

    smt::Term propagated = SimplePropagate(ctx)(formula);

    bool saw_substituted_gt = false;
    for (auto it = propagated->begin(); it != propagated->end(); ++it) {
        if (is_gt(*it) && get_child(*it, 0)->is_value()
            && term_to_cpp_int(get_child(*it, 0)) == 2) {
            saw_substituted_gt = true;
        }
    }
    expect_true(saw_substituted_gt,
                "SimplePropagate did not substitute equality into sibling atom");
}

int main() {
    Ctx ctx = make_test_ctx();
    test_flatten_mul(ctx);
    test_simple_simplify_arithmetic(ctx);
    test_simple_propagate(ctx);
    return 0;
}

