#pragma once
#include "prefix.h"
#include <unordered_map>

// Track which quantifier level each constant belongs to,
// and compute the max level of any sub-term.
class GetLevel {
public:
    GetLevel(const Ctx& ctx,
             const std::unordered_map<smt::Term, int>& const2lev,
             const smt::Term& root);

    int operator()(const smt::Term& t);

    // All terms with level >= 0 seen during traversal.
    const std::unordered_map<smt::Term, int>& terms() const {
        return _terms;
    }

private:
    const Ctx& _ctx;
    const std::unordered_map<smt::Term, int>& _const2lev;
    std::unordered_map<smt::Term, int> _memo;
    std::unordered_map<smt::Term, int> _terms;

    int compute(const smt::Term& t);
};

// Bookkeeping for quantification levels of constants in the formula.
struct FormulaInfo {
    Prefix prefix;
    smt::Term body;

    FormulaInfo(const Ctx& ctx, Prefix p, smt::Term b);

    void add_const(const smt::Term& c, int lev);
    int  get_level(const smt::Term& t);
    const std::unordered_map<smt::Term, int>& get_terms();

private:
    const Ctx& _ctx;
    std::unordered_map<smt::Term, int> _const2lev;
    GetLevel _get_level;
};
