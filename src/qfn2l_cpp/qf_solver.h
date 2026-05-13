#pragma once
#include "lia_abstraction.h"
#include <memory>

class QfSolver {
  public:
    QfSolver(const Ctx& ctx, const Options& opts, const smt::Term& formula);

    // Returns true (sat), false (unsat), or nullopt (unknown/maxits reached).
    std::optional<bool> solve();

    const LiaAbstraction& abstraction() const { return *_abstraction; }

  private:
    const Ctx&                      _ctx;
    const Options&                  _opts;
    std::unique_ptr<LiaAbstraction> _abstraction;
};
