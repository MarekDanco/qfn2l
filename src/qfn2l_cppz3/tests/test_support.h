#pragma once

#include "utils.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

inline Ctx make_test_ctx() { return Ctx{}; }

inline void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

inline void expect_false(bool condition, const std::string& message) {
    expect_true(!condition, message);
}

inline z3::check_result check_formula(const Ctx& ctx, const Term& formula) {
    z3::solver slv(ctx.zctx);
    slv.add(formula);
    return slv.check();
}

inline void expect_solver_sat(const Ctx& ctx, const Term& formula,
                               const std::string& message) {
    expect_true(check_formula(ctx, formula) == z3::sat, message);
}

inline void expect_solver_unsat(const Ctx& ctx, const Term& formula,
                                 const std::string& message) {
    expect_true(check_formula(ctx, formula) == z3::unsat, message);
}

inline void expect_solver_valid(const Ctx& ctx, const Term& formula,
                                 const std::string& message) {
    expect_solver_unsat(ctx, mk_not(ctx, formula), message);
}
