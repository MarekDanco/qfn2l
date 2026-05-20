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

static void expect_substituted_axiom_sat(Ctx& ctx, const smt::Term& axiom,
                                         const smt::UnorderedTermMap& subst,
                                         const std::string& message) {
    expect_solver_sat(ctx, do_substitute(ctx, axiom, subst), message);
}

static void expect_substituted_axiom_unsat(Ctx& ctx, const smt::Term& axiom,
                                           const smt::UnorderedTermMap& subst,
                                           const std::string& message) {
    expect_solver_unsat(ctx, do_substitute(ctx, axiom, subst), message);
}

static void test_modular_axiom_generation(Ctx& ctx) {
    smt::Term x = ctx.solver->make_symbol("mx", ctx.int_sort);
    smt::Term pure = ctx.solver->make_symbol("mp", ctx.int_sort);

    auto none = mod_ax_mul(ctx, 11, {{x, 2, ctx.make_int(4)}}, pure, ctx.make_int(16));
    expect_true(none.empty(), "matching residue generated an unnecessary axiom");

    auto axs = mod_ax_mul(ctx, 11, {{x, 2, ctx.make_int(4)}}, pure, ctx.make_int(15));
    expect_true(axs.size() == 1, "mismatching residue did not generate one axiom");
    expect_true(is_eq(axs[0]), "zero-residue modular axiom should be an iff");

    smt::UnorderedTermMap bad_residue;
    bad_residue[x] = ctx.make_int(4);
    bad_residue[pure] = ctx.make_int(15);
    expect_substituted_axiom_unsat(
        ctx, axs[0], bad_residue,
        "modular axiom did not reject an inconsistent pure residue");

    smt::Term y = ctx.solver->make_symbol("my", ctx.int_sort);
    auto even_axs = mod_ax_mul(ctx, 11, {{x, 1, ctx.make_int(4)},
                                         {y, 1, ctx.make_int(5)}},
                               pure, ctx.make_int(19));
    expect_true(even_axs.size() == 1, "even product did not generate an axiom");
    expect_true(is_eq(even_axs[0]), "even-product modular axiom should be an iff");
    smt::UnorderedTermMap bad_even_product;
    bad_even_product[x] = ctx.make_int(4);
    bad_even_product[y] = ctx.make_int(5);
    bad_even_product[pure] = ctx.make_int(19);
    expect_substituted_axiom_unsat(
        ctx, even_axs[0], bad_even_product,
        "even-product iff did not reject an odd pure");

    smt::UnorderedTermMap good_even_product;
    good_even_product[x] = ctx.make_int(4);
    good_even_product[y] = ctx.make_int(5);
    good_even_product[pure] = ctx.make_int(20);
    expect_substituted_axiom_sat(ctx, even_axs[0], good_even_product,
                                 "even-product iff rejected an even pure");

    auto odd_axs = mod_ax_mul(ctx, 11, {{x, 1, ctx.make_int(3)},
                                        {y, 1, ctx.make_int(5)}},
                              pure, ctx.make_int(14));
    expect_true(odd_axs.size() == 1, "odd product did not generate an axiom");
    expect_true(is_eq(odd_axs[0]), "odd-product modular axiom should be an iff");
    smt::UnorderedTermMap bad_odd_product;
    bad_odd_product[x] = ctx.make_int(3);
    bad_odd_product[y] = ctx.make_int(5);
    bad_odd_product[pure] = ctx.make_int(14);
    expect_substituted_axiom_unsat(
        ctx, odd_axs[0], bad_odd_product,
        "odd-product iff did not reject an even pure");

    smt::UnorderedTermMap good_odd_product;
    good_odd_product[x] = ctx.make_int(3);
    good_odd_product[y] = ctx.make_int(5);
    good_odd_product[pure] = ctx.make_int(15);
    expect_substituted_axiom_sat(ctx, odd_axs[0], good_odd_product,
                                 "odd-product iff rejected an odd pure");

    smt::UnorderedTermMap good_odd_product_false_branch;
    good_odd_product_false_branch[x] = ctx.make_int(4);
    good_odd_product_false_branch[y] = ctx.make_int(5);
    good_odd_product_false_branch[pure] = ctx.make_int(14);
    expect_substituted_axiom_sat(
        ctx, odd_axs[0], good_odd_product_false_branch,
        "odd-product iff rejected the false=false branch");

    auto odd_power_axs =
        mod_ax_mul(ctx, 11, {{x, 3, ctx.make_int(3)}}, pure, ctx.make_int(26));
    expect_true(odd_power_axs.size() == 1, "odd power did not generate an axiom");
    expect_true(is_eq(odd_power_axs[0]), "odd-power parity axiom should be an iff");
    smt::UnorderedTermMap bad_odd_power;
    bad_odd_power[x] = ctx.make_int(3);
    bad_odd_power[pure] = ctx.make_int(26);
    expect_substituted_axiom_unsat(
        ctx, odd_power_axs[0], bad_odd_power,
        "odd-power parity iff did not reject an even pure");

    auto prime_zero_axs =
        mod_ax_mul(ctx, 3, {{x, 1, ctx.make_int(3)}, {y, 1, ctx.make_int(2)}},
                   pure, ctx.make_int(8));
    expect_true(prime_zero_axs.size() == 1, "prime-zero product did not generate an axiom");
    expect_true(is_eq(prime_zero_axs[0]), "prime-zero modular axiom should be an iff");
    smt::UnorderedTermMap bad_prime_zero;
    bad_prime_zero[x] = ctx.make_int(3);
    bad_prime_zero[y] = ctx.make_int(2);
    bad_prime_zero[pure] = ctx.make_int(8);
    expect_substituted_axiom_unsat(
        ctx, prime_zero_axs[0], bad_prime_zero,
        "prime-zero iff did not reject a nonzero pure residue");

    smt::UnorderedTermMap good_prime_zero_true_branch;
    good_prime_zero_true_branch[x] = ctx.make_int(3);
    good_prime_zero_true_branch[y] = ctx.make_int(2);
    good_prime_zero_true_branch[pure] = ctx.make_int(6);
    expect_substituted_axiom_sat(
        ctx, prime_zero_axs[0], good_prime_zero_true_branch,
        "prime-zero iff rejected the true=true branch");

    smt::UnorderedTermMap good_prime_zero_false_branch;
    good_prime_zero_false_branch[x] = ctx.make_int(1);
    good_prime_zero_false_branch[y] = ctx.make_int(2);
    good_prime_zero_false_branch[pure] = ctx.make_int(2);
    expect_substituted_axiom_sat(
        ctx, prime_zero_axs[0], good_prime_zero_false_branch,
        "prime-zero iff rejected the false=false branch");

    auto composite_zero_axs =
        mod_ax_mul(ctx, 4, {{x, 1, ctx.make_int(2)}, {y, 1, ctx.make_int(2)}},
                   pure, ctx.make_int(10));
    expect_true(composite_zero_axs.size() == 1,
                "composite-zero product did not generate an axiom");
    expect_true(is_implies(composite_zero_axs[0]),
                "composite-zero modular axiom should stay an implication");
    smt::UnorderedTermMap bad_composite_zero;
    bad_composite_zero[x] = ctx.make_int(2);
    bad_composite_zero[y] = ctx.make_int(2);
    bad_composite_zero[pure] = ctx.make_int(10);
    expect_substituted_axiom_unsat(
        ctx, composite_zero_axs[0], bad_composite_zero,
        "composite-zero implication did not reject a nonzero pure residue");

    auto fallback_axs =
        mod_ax_mul(ctx, 3, {{x, 1, ctx.make_int(1)}, {y, 1, ctx.make_int(2)}},
                   pure, ctx.make_int(4));
    expect_true(fallback_axs.size() == 1, "non-special residue did not generate an axiom");
    expect_true(is_implies(fallback_axs[0]),
                "non-special modular axiom should stay an implication");
    smt::UnorderedTermMap bad_fallback;
    bad_fallback[x] = ctx.make_int(1);
    bad_fallback[y] = ctx.make_int(2);
    bad_fallback[pure] = ctx.make_int(4);
    expect_substituted_axiom_unsat(
        ctx, fallback_axs[0], bad_fallback,
        "non-special implication did not reject a bad pure residue");

    smt::UnorderedTermMap good_fallback;
    good_fallback[x] = ctx.make_int(1);
    good_fallback[y] = ctx.make_int(2);
    good_fallback[pure] = ctx.make_int(2);
    expect_substituted_axiom_sat(ctx, fallback_axs[0], good_fallback,
                                 "non-special implication rejected a matching residue");
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
