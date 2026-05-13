#include "tests/test_support.h"
#include "pures.h"

#include <unordered_map>

static void test_collect_pures_through_axioms(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("x", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("y", ctx.int_sort);
    smt::Term z = ctx.solver->make_symbol("z", ctx.int_sort);

    smt::Term mul = ctx.solver->make_term(smt::Mult, x, y);
    smt::Term p = ctx.fresh_symbol(ctx.int_sort, "p");
    smt::Term mod = ctx.solver->make_term(smt::Mod, p, z);
    smt::Term q = ctx.fresh_symbol(ctx.int_sort, "q");

    Pures pures;
    pures.map_t2p(mul, p);
    pures.map_t2p(mod, q);

    std::unordered_map<smt::Term, smt::TermVec> axioms;
    axioms[q].push_back(ctx.solver->make_term(smt::Equal, p, ctx.ONE));

    smt::Term formula = ctx.solver->make_term(smt::Equal, q, ctx.ZERO);
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

