#pragma once
#include "visitors.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>

// Bidirectional map: NIA term ↔ pure constant.
class Pures {
  public:
    void map_t2p(const Term& t, const Term& p);

    Term* find_p(const Term& t);
    Term* find_t(const Term& p);
    const Term& get_p(const Term& t) const;
    const Term& get_t(const Term& p) const;

    const TermMap& term2pure() const { return _term2pure; }

  private:
    TermMap _term2pure;
    TermMap _pure2term;
};

// Traverse a formula collecting active pure constants.
class CollectPures {
  public:
    CollectPures(const Ctx& ctx, const Pures& pures,
                 const std::unordered_map<Term, TermVec, ExprHash, ExprEq>& axioms);

    void operator()(const Term& t);

    TermSet collected;
    TermSet idiv_collected;
    TermSet mod_collected;
    TermSet mul_collected;

  private:
    const Ctx&   _ctx;
    const Pures& _pures;
    const std::unordered_map<Term, TermVec, ExprHash, ExprEq>& _axioms;
    TermSet _visited;

    void visit(const Term& t);
};

// Three-valued evaluation under the current LIA model.
class CheckVal {
  public:
    CheckVal(const Ctx& ctx, HasUninterpreted& hu, const Pures& pures);

    bool check(const Term& formula);
    std::optional<Term> operator()(const Term& t);

  private:
    const Ctx&       _ctx;
    HasUninterpreted& _hu;
    const Pures&     _pures;
    std::unordered_map<Term, std::optional<Term>, ExprHash, ExprEq> _memo;

    std::optional<Term> visit(const Term& t);
    std::optional<Term> visit_purified(const Term& orig, const Term& pure);
    std::optional<Term> visit_leaf(const Term& t);
    std::optional<Term> visit_prop(const Term& t,
                                   const std::vector<std::optional<Term>>& cvs);
    std::optional<Term> visit_complex(const Term& t,
                                      const std::vector<std::optional<Term>>& cvs);
};
