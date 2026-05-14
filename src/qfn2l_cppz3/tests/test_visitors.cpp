#include "tests/test_support.h"
#include "visitors.h"

static int count_child(const Term& t, const Term& child) {
    int count = 0;
    for (unsigned i = 0; i < t.num_args(); ++i)
        if (t.arg(i).id() == child.id())
            ++count;
    return count;
}

static void test_flatten_mul(Ctx& ctx) {
    Term x = ctx.fresh_symbol(ctx.int_sort, "x");
    Term y = ctx.fresh_symbol(ctx.int_sort, "y");
    Term z = ctx.fresh_symbol(ctx.int_sort, "z");

    // Build nested (x * (y * z)) via z3 operator*
    Term yz     = mk_mul(ctx, {y, z});
    Term nested = mk_mul(ctx, {x, yz});
    Term flat   = FlattenMul(ctx)(nested);

    expect_true(is_mul(flat), "flattened term is not multiplication");
    expect_true(num_children(flat) == 3, "nested multiplication was not flattened");
    expect_true(count_child(flat, x) == 1, "x missing or duplicated after flattening");
    expect_true(count_child(flat, y) == 1, "y missing or duplicated after flattening");
    expect_true(count_child(flat, z) == 1, "z missing or duplicated after flattening");
}

static void test_simple_simplify_arithmetic(Ctx& ctx) {
    Term x = ctx.fresh_symbol(ctx.int_sort, "sx");

    // 0 + 2 + (3 + x)
    Term inner = mk_add(ctx, {ctx.make_int(3), x});
    Term sum   = mk_add(ctx, {ctx.ZERO, ctx.make_int(2), inner});
    Term simplified_sum = SimpleSimplify(ctx)(sum);

    expect_true(is_add(simplified_sum), "simplified sum should still contain x");
    expect_true(num_children(simplified_sum) == 2,
                "simplified sum should contain one coefficient and one symbol");

    bool saw_five = false;
    bool saw_x    = false;
    for (unsigned i = 0; i < simplified_sum.num_args(); ++i) {
        Term ch = simplified_sum.arg(i);
        if (ch.id() == x.id())
            saw_x = true;
        if (ch.is_numeral() && term_to_cpp_int(ch) == 5)
            saw_five = true;
    }
    expect_true(saw_x,    "simplified sum lost symbolic child");
    expect_true(saw_five, "simplified sum did not fold numeric children");

    Term product = mk_mul(ctx, {ctx.ONE, ctx.ZERO, x});
    Term simplified_product = SimpleSimplify(ctx)(product);
    expect_true(simplified_product.is_numeral() && z3::eq(simplified_product, ctx.ZERO),
                "multiplication by zero did not simplify to zero");
}

static void test_simple_propagate(Ctx& ctx) {
    Term x = ctx.fresh_symbol(ctx.int_sort, "px");
    // x == 2 AND x > 0  →  SimplePropagate substitutes 2 for x in x > 0
    Term eq      = (x == ctx.make_int(2));
    Term gt      = (x > ctx.ZERO);
    Term formula = mk_and(ctx, {eq, gt});

    Term propagated = SimplePropagate(ctx)(formula);

    // After propagation, one conjunct should be (2 > 0)
    bool saw_substituted_gt = false;
    for (unsigned i = 0; i < propagated.num_args(); ++i) {
        Term ch = propagated.arg(i);
        if (is_gt(ch) && ch.arg(0).is_numeral() && term_to_cpp_int(ch.arg(0)) == 2)
            saw_substituted_gt = true;
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
