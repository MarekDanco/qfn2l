#pragma once

#include "utils.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef BACKEND_Z3
#include "z3_factory.h"
#include "z3_solver.h"
#include "z3_term.h"
#endif

inline Ctx make_test_ctx() {
#ifdef BACKEND_Z3
    smt::SmtSolver solver = smt::Z3SolverFactory::create(false);
    solver->set_opt("produce-models", "true");
    return Ctx(solver);
#else
    throw std::runtime_error("unit tests currently require BACKEND_Z3");
#endif
}

inline void expect_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

inline void expect_false(bool condition, const std::string& message) {
    expect_true(!condition, message);
}

inline smt::Result check_formula(Ctx& ctx, const smt::Term& formula) {
#ifdef BACKEND_Z3
    if (auto* z3s = dynamic_cast<smt::Z3Solver*>(ctx.solver.get())) {
        auto* z3t = dynamic_cast<smt::Z3Term*>(formula.get());
        expect_true(z3t != nullptr, "expected a Z3-backed smt-switch term");

        z3::solver slv(*z3s->get_z3_context());
        slv.add(z3t->get_z3_expr());
        z3::check_result res = slv.check();
        if (res == z3::sat)
            return smt::Result(smt::SAT);
        if (res == z3::unsat)
            return smt::Result(smt::UNSAT);
        return smt::Result(smt::UNKNOWN);
    }
#endif
    ctx.solver->assert_formula(formula);
    return ctx.solver->check_sat();
}

inline void expect_solver_sat(Ctx& ctx, const smt::Term& formula,
                              const std::string& message) {
    expect_true(check_formula(ctx, formula).is_sat(), message);
}

inline void expect_solver_unsat(Ctx& ctx, const smt::Term& formula,
                                const std::string& message) {
    expect_true(check_formula(ctx, formula).is_unsat(), message);
}

inline void expect_solver_valid(Ctx& ctx, const smt::Term& formula,
                                const std::string& message) {
    expect_solver_unsat(ctx, mk_not(ctx, formula), message);
}
