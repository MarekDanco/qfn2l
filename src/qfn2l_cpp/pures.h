#pragma once
#include "visitors.h"
#include <optional>
#include <unordered_map>

// Bidirectional map between NIA terms and their pure constants.
class Pures {
  public:
    void map_t2p(const smt::Term& t, const smt::Term& p);

    smt::Term* find_p(const smt::Term& t); // nullptr if not found
    smt::Term* find_t(const smt::Term& p); // nullptr if not found
    const smt::Term* find_p(const smt::Term& t) const; // nullptr if not found
    const smt::Term* find_t(const smt::Term& p) const; // nullptr if not found
    const smt::Term& get_p(const smt::Term& t) const;
    const smt::Term& get_t(const smt::Term& p) const;

    const smt::UnorderedTermMap& term2pure() const { return _term2pure; }

  private:
    smt::UnorderedTermMap _term2pure;
    smt::UnorderedTermMap _pure2term;
};

// Traverse a formula collecting active pure constants (those reachable
// from the formula or from their own axioms).
class CollectPures {
  public:
    CollectPures(const Ctx& ctx, const Pures& pures,
                 const std::unordered_map<smt::Term, smt::TermVec>& axioms);

    void operator()(const smt::Term& t);

    smt::UnorderedTermSet collected;
    smt::UnorderedTermSet idiv_collected;
    smt::UnorderedTermSet mod_collected;
    smt::UnorderedTermSet mul_collected;

  private:
    const Ctx& _ctx;
    const Pures& _pures;
    const std::unordered_map<smt::Term, smt::TermVec>& _axioms;
    smt::UnorderedTermSet _visited;

    void visit(const smt::Term& t);
};

// Evaluate a term under the current LIA model, producing an optional concrete
// value.  Returns nullopt when the value cannot be determined (e.g. because a
// pure constant doesn't yet have an assignment, or division by zero).
//
// Three-valued: returns TRUE_T/FALSE_T for bool, int Term for integers,
// or nullopt for unknown.
class CheckVal {
  public:
    struct ModelFixInfo {
        smt::TermVec implicant;
        smt::UnorderedTermSet wrong_pures;
        std::unordered_map<smt::Term, smt::TermVec> adjustable_vars;
        // Variables appearing in implicant literals that contain a wrong pure
        // (expanding through pures to leaf vars). These form the "free zone"
        // for the subiteration fix: everything outside gets pinned.
        smt::UnorderedTermSet relevant_vars;
    };

    CheckVal(const Ctx& ctx, HasUninterpreted& hu, const Pures& pures,
             const smt::SmtSolver& lia_solver);

    // Returns true iff formula evaluates to true under the model.
    bool check(const smt::Term& formula);
    ModelFixInfo model_fix_info(const smt::Term& formula);

    std::optional<smt::Term> operator()(const smt::Term& t);

  private:
    const Ctx& _ctx;
    HasUninterpreted& _hu;
    const Pures& _pures;
    const smt::SmtSolver& _lia_solver;
    std::unordered_map<smt::Term, std::optional<smt::Term>> _memo;

    std::optional<smt::Term> visit(const smt::Term& t);
    std::optional<smt::Term> visit_purified(const smt::Term& orig,
                                            const smt::Term& pure);
    std::optional<smt::Term> visit_leaf(const smt::Term& t);
    std::optional<smt::Term>
    visit_prop(const smt::Term& t, const std::vector<std::optional<smt::Term>>& cvs);
    std::optional<smt::Term>
    visit_complex(const smt::Term& t, const std::vector<std::optional<smt::Term>>& cvs);

    std::optional<smt::Term> model_value(const smt::Term& t) const;
    bool collect_implicant(const smt::Term& formula, smt::TermVec& out) const;
    smt::UnorderedTermSet pures_in(const smt::Term& t) const;
    bool pure_is_wrong(const smt::Term& pure);
    bool contains_var_expanded(const smt::Term& t, const smt::Term& var,
                               const smt::Term& skip_pure) const;
    smt::TermVec adjustable_vars_for(const smt::Term& pure,
                                     const smt::TermVec& implicant) const;
    // Collect all leaf variables reachable from root, expanding through pures.
    smt::UnorderedTermSet vars_in_expanded(const smt::Term& root) const;
};
