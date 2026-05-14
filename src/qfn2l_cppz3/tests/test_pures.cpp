#include "tests/test_support.h"
#include "pures.h"

static void test_collect_pures_through_axioms(Ctx& ctx) {
    Term x = ctx.fresh_symbol(ctx.int_sort, "x");
    Term y = ctx.fresh_symbol(ctx.int_sort, "y");
    Term z = ctx.fresh_symbol(ctx.int_sort, "z");

    Term mul = mk_mul(ctx, {x, y});
    Term p   = ctx.fresh_symbol(ctx.int_sort, "p");
    Term mod = z3::mod(p, z);
    Term q   = ctx.fresh_symbol(ctx.int_sort, "q");

    Pures pures;
    pures.map_t2p(mul, p);
    pures.map_t2p(mod, q);

    std::unordered_map<Term, TermVec, ExprHash, ExprEq> axioms;
    axioms[q].push_back((p == ctx.ONE));

    Term formula = (q == ctx.ZERO);
    CollectPures collected(ctx, pures, axioms);
    collected(formula);

    expect_true(collected.collected.count(q), "direct pure was not collected");
    expect_true(collected.collected.count(p), "pure reachable through axiom was not collected");
    expect_true(collected.mod_collected.count(q), "mod pure was not categorized");
    expect_true(collected.mul_collected.count(p), "mul pure was not categorized");
}

int main() {
    Ctx ctx = make_test_ctx();
    test_collect_pures_through_axioms(ctx);
    return 0;
}
