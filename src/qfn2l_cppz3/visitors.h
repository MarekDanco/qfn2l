#pragma once
#include "prefix.h"
#include <functional>
#include <optional>
#include <unordered_map>

// ── Base: memoized term transformer ──────────────────────────────────────────
class TermTransformer {
  public:
    explicit TermTransformer(const Ctx& ctx) : _ctx(ctx) {}
    virtual ~TermTransformer() = default;

    Term operator()(const Term& t);
    // Recursively apply this transformer to all children, then rebuild.
    Term recurse(const Term& t);

  protected:
    virtual Term visit_node(const Term& t) = 0;
    const Ctx& _ctx;

  private:
    TermMap _memo;
};

// ── Base: memoized predicate ──────────────────────────────────────────────────
class TermPredicate {
  public:
    explicit TermPredicate(const Ctx& ctx) : _ctx(ctx) {}
    virtual ~TermPredicate() = default;

    bool operator()(const Term& t);

  protected:
    virtual bool visit_node(const Term& t) = 0;
    const Ctx& _ctx;

  private:
    std::unordered_map<Term, bool, ExprHash, ExprEq> _memo;
};

// ── HasUninterpreted ───────────────────────────────────────────────────────────
class HasUninterpreted : public TermPredicate {
  public:
    explicit HasUninterpreted(const Ctx& ctx) : TermPredicate(ctx) {}
  protected:
    bool visit_node(const Term& t) override;
};

// ── Contains ─────────────────────────────────────────────────────────────────
class Contains : public TermPredicate {
  public:
    Contains(const Ctx& ctx, const TermSet& cs) : TermPredicate(ctx), _cs(cs) {}
  protected:
    bool visit_node(const Term& t) override;
  private:
    const TermSet& _cs;
};

// ── SimpleSimplify ────────────────────────────────────────────────────────────
class SimpleSimplify : public TermTransformer {
  public:
    explicit SimpleSimplify(const Ctx& ctx) : TermTransformer(ctx) {}
  protected:
    Term visit_node(const Term& t) override;
  private:
    Term visit_add(const Term& t);
    Term visit_mul(const Term& t);
    Term visit_or(const Term& t);
    Term visit_and(const Term& t);
    Term visit_sub(const Term& t);
    Term visit_ite(const Term& t);
};

// ── SimplePropagate ───────────────────────────────────────────────────────────
class SimplePropagate : public TermTransformer {
  public:
    explicit SimplePropagate(const Ctx& ctx) : TermTransformer(ctx) {}
  protected:
    Term visit_node(const Term& t) override;
  private:
    Term propagate(bool pos, const Term& t);
};

// ── FlattenMul ────────────────────────────────────────────────────────────────
class FlattenMul : public TermTransformer {
  public:
    explicit FlattenMul(const Ctx& ctx) : TermTransformer(ctx) {}
  protected:
    Term visit_node(const Term& t) override;
};

// ── MakeDefs ──────────────────────────────────────────────────────────────────
class MakeDefs : public TermTransformer {
  public:
    explicit MakeDefs(const Ctx& ctx);

    // Apply to a formula and update the prefix with fresh existential vars.
    std::pair<Prefix, Term> make(const Prefix& in_prefix, const Term& formula);

    const TermMap& definitions() const { return _definitions; }

  protected:
    Term visit_node(const Term& t) override;

  private:
    HasUninterpreted _hu;
    TermMap          _definitions;

    Term mk_def(const Term& t);
};
