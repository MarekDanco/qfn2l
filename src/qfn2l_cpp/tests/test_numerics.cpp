#include "utils.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#ifdef BACKEND_Z3
#include "z3_factory.h"
#endif

using boost::multiprecision::cpp_int;

static Ctx make_test_ctx() {
#ifdef BACKEND_Z3
    return Ctx(smt::Z3SolverFactory::create(false));
#else
    throw std::runtime_error("numeric tests currently require BACKEND_Z3");
#endif
}

static void expect_cpp_int(const smt::Term& t, const cpp_int& expected) {
    cpp_int actual = term_to_cpp_int(t);
    if (actual != expected) {
        std::cerr << "expected " << expected << ", got " << actual << " from "
                  << t->to_string() << "\n";
        std::exit(1);
    }
}

static void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

static void test_round_trip_big_values(Ctx& ctx) {
    cpp_int big("1234567890123456789012345678901234567890");

    expect_cpp_int(ctx.make_int_str(big.str()), big);
    expect_cpp_int(ctx.make_int_str("-" + big.str()), -big);
    expect_cpp_int(ctx.make_int_str("(- " + big.str() + ")"), -big);

    smt::Term t = cpp_int_to_term(ctx, -big);
    expect_cpp_int(t, -big);
}

static void test_checked_int64_narrowing(Ctx& ctx) {
    cpp_int max64 = std::numeric_limits<int64_t>::max();
    cpp_int min64 = std::numeric_limits<int64_t>::min();

    expect_true(term_to_int64(cpp_int_to_term(ctx, max64)) ==
                    std::numeric_limits<int64_t>::max(),
                "max int64_t did not round trip");
    expect_true(term_to_int64(cpp_int_to_term(ctx, min64)) ==
                    std::numeric_limits<int64_t>::min(),
                "min int64_t did not round trip");

    bool threw = false;
    try {
        (void)term_to_int64(cpp_int_to_term(ctx, max64 + 1));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect_true(threw, "max int64_t + 1 did not throw");

    threw = false;
    try {
        (void)term_to_int64(cpp_int_to_term(ctx, min64 - 1));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect_true(threw, "min int64_t - 1 did not throw");
}

static void test_constant_folding(Ctx& ctx) {
    cpp_int a("1000000000000000000000000000000");
    cpp_int b("999999999999999999999999999999");

    smt::Term ta = cpp_int_to_term(ctx, a);
    smt::Term tb = cpp_int_to_term(ctx, b);

    expect_cpp_int(eval_sum(ctx, {ta, tb, ctx.ONE}), a + b + 1);
    expect_cpp_int(eval_mul(ctx, {ta, tb, ctx.MIN_ONE}), -(a * b));
    expect_cpp_int(eval_exp(ctx, cpp_int_to_term(ctx, -12), 9), cpp_int("-5159780352"));
    expect_cpp_int(negate_numeral(ctx, ta), -a);
}

static void test_mod_residue(Ctx& ctx) {
    cpp_int big("-1234567890123456789012345678901234567890");
    smt::Term t = cpp_int_to_term(ctx, big);

    expect_true(term_mod_int(ctx, t, 7) == 4, "unexpected modulo-7 residue");
    expect_true(term_mod_int(ctx, t, 11) == 9, "unexpected modulo-11 residue");
    expect_true(term_mod_int(ctx, t, 97) == 69, "unexpected modulo-97 residue");
}

int main() {
    Ctx ctx = make_test_ctx();

    test_round_trip_big_values(ctx);
    test_checked_int64_narrowing(ctx);
    test_constant_folding(ctx);
    test_mod_residue(ctx);

    return 0;
}
