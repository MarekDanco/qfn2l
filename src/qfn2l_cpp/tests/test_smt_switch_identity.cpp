#include "tests/test_support.h"

#include <string>

static void expect_distinct_terms(const smt::Term& a, const smt::Term& b,
                                  const std::string& label) {
    expect_false(a == b, label + ": terms compare equal");
    expect_true(a != b, label + ": terms do not compare distinct");

    smt::UnorderedTermSet set;
    set.insert(a);
    set.insert(b);
    expect_true(set.size() == 2, label + ": UnorderedTermSet collapsed terms");
    expect_true(set.count(a) == 1, label + ": set lost first term");
    expect_true(set.count(b) == 1, label + ": set lost second term");

    smt::UnorderedTermMap map;
    map[a] = a;
    map[b] = b;
    expect_true(map.size() == 2, label + ": UnorderedTermMap collapsed keys");
    expect_true(map.at(a) == a, label + ": map lookup for first key is wrong");
    expect_true(map.at(b) == b, label + ": map lookup for second key is wrong");
}

static void test_signed_numeral_identity(Ctx& ctx) {
    smt::Term pos9_i64 = ctx.make_int(9);
    smt::Term neg9_i64 = ctx.make_int(-9);
    smt::Term pos9_str = ctx.make_int_str("9");
    smt::Term neg9_str = ctx.make_int_str("-9");
    smt::Term neg9_sexpr = ctx.make_int_str("(- 9)");
    smt::Term pos10 = ctx.make_int(10);
    smt::Term neg10 = ctx.make_int(-10);

    expect_true(pos9_i64 == pos9_str, "9 built two ways should compare equal");

    expect_distinct_terms(pos9_i64, neg9_i64, "9 vs -9");
    expect_distinct_terms(pos9_str, neg9_str, "string 9 vs string -9");
    expect_distinct_terms(pos10, neg10, "10 vs -10");
    expect_distinct_terms(ctx.ZERO, neg9_i64, "0 vs -9");

    expect_solver_valid(ctx, ctx.solver->make_term(smt::Equal, neg9_i64, neg9_str),
                        "-9 built two ways should be semantically equal");
    expect_solver_valid(ctx, ctx.solver->make_term(smt::Equal, neg9_i64, neg9_sexpr),
                        "-9 and SMT-LIB (- 9) should be semantically equal");

    expect_true(term_to_cpp_int(pos9_i64) == 9, "positive 9 parsed incorrectly");
    expect_true(term_to_cpp_int(neg9_i64) == -9, "negative 9 parsed incorrectly");
}

int main() {
    Ctx ctx = make_test_ctx();
    test_signed_numeral_identity(ctx);
    return 0;
}
