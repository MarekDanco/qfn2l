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

static void test_model_fix_info_finds_adjustable_vars() {
    Ctx ctx = make_test_ctx();
    smt::Term x = ctx.solver->make_symbol("x", ctx.int_sort);
    smt::Term y = ctx.solver->make_symbol("y", ctx.int_sort);
    smt::Term mul = ctx.solver->make_term(smt::Mult, x, y);
    smt::Term p = ctx.fresh_symbol(ctx.int_sort, "p");

    Pures pures;
    pures.map_t2p(mul, p);

    smt::Term lit = ctx.solver->make_term(smt::Gt, p, x);
    smt::Term other = ctx.solver->make_term(smt::Gt, y, ctx.make_int(100));
    smt::Term formula = ctx.solver->make_term(smt::Or, lit, other);

    smt::TermVec model_eqs = {
        ctx.solver->make_term(smt::Equal, x, ctx.make_int(2)),
        ctx.solver->make_term(smt::Equal, y, ctx.make_int(3)),
        ctx.solver->make_term(smt::Equal, p, ctx.make_int(10)),
        formula,
    };
    ctx.solver->assert_formula(mk_and(ctx, model_eqs));
    expect_true(ctx.solver->check_sat().is_sat(), "expected model-fix setup SAT");

    HasUninterpreted hu(ctx);
    CheckVal cv(ctx, hu, pures, ctx.solver);
    CheckVal::ModelFixInfo info = cv.model_fix_info(formula);

    expect_true(info.implicant.size() == 1 && info.implicant[0] == lit,
                "expected the true disjunct as the implicant");
    expect_true(info.wrong_pures.count(p), "expected p to be reported as wrong");

    auto it = info.adjustable_vars.find(p);
    expect_true(it != info.adjustable_vars.end(), "expected adjustable vars for p");
    smt::UnorderedTermSet adjustable(it->second.begin(), it->second.end());
    expect_false(adjustable.count(x), "x appears elsewhere in the implicant");
    expect_true(adjustable.count(y), "y should be adjustable");
}

int main() {
    Ctx ctx = make_test_ctx();
    test_collect_pures_through_axioms(ctx);
    test_model_fix_info_finds_adjustable_vars();
    return 0;
}
