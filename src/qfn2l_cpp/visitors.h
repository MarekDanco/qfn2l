#pragma once
#include "prefix.h"
#include <functional>
#include <optional>
#include <unordered_map>

// ── Base: memoized term transformer ──────────────────────────────────────────
// Subclass and override visit_node(); call via operator().
class TermTransformer {
public:
    explicit TermTransformer(const Ctx& ctx) : _ctx(ctx) {}
    virtual ~TermTransformer() = default;

    smt::Term operator()(const smt::Term& t);
    // Recursively apply this transformer to all children, then rebuild.
    smt::Term recurse(const smt::Term& t);

protected:
    virtual smt::Term visit_node(const smt::Term& t) = 0;
    const Ctx& _ctx;

private:
    smt::UnorderedTermMap _memo;
};

// ── Base: memoized predicate ──────────────────────────────────────────────────
class TermPredicate {
public:
    explicit TermPredicate(const Ctx& ctx) : _ctx(ctx) {}
    virtual ~TermPredicate() = default;

    bool operator()(const smt::Term& t);

protected:
    virtual bool visit_node(const smt::Term& t) = 0;
    const Ctx& _ctx;

private:
    std::unordered_map<smt::Term, bool> _memo;
};

// ── HasUninterpreted: does a term contain any symbolic constant? ───────────────
class HasUninterpreted : public TermPredicate {
public:
    explicit HasUninterpreted(const Ctx& ctx) : TermPredicate(ctx) {}
protected:
    bool visit_node(const smt::Term& t) override;
};

// ── Contains: does a term contain any member of a given set? ─────────────────
class Contains : public TermPredicate {
public:
    Contains(const Ctx& ctx, const smt::UnorderedTermSet& cs)
        : TermPredicate(ctx), _cs(cs) {}
protected:
    bool visit_node(const smt::Term& t) override;
private:
    const smt::UnorderedTermSet& _cs;
};

// ── SimpleSimplify: structural constant-folding simplification ───────────────
class SimpleSimplify : public TermTransformer {
public:
    explicit SimpleSimplify(const Ctx& ctx) : TermTransformer(ctx) {}
protected:
    smt::Term visit_node(const smt::Term& t) override;
private:
    smt::Term visit_add(const smt::Term& t);
    smt::Term visit_mul(const smt::Term& t);
    smt::Term visit_or(const smt::Term& t);
    smt::Term visit_and(const smt::Term& t);
    smt::Term visit_sub(const smt::Term& t);
    smt::Term visit_ite(const smt::Term& t);
};

// ── SimplePropagate: propagate equalities within And/Or ──────────────────────
class SimplePropagate : public TermTransformer {
public:
    explicit SimplePropagate(const Ctx& ctx) : TermTransformer(ctx) {}
protected:
    smt::Term visit_node(const smt::Term& t) override;
private:
    smt::Term propagate(bool pos, const smt::Term& t);
};

// ── MakeDefs: introduce fresh constants for nonlinear mul sub-terms ───────────
class MakeDefs : public TermTransformer {
public:
    explicit MakeDefs(const Ctx& ctx);

    // Apply to a formula and update the prefix with fresh existential vars.
    // Returns {new_prefix, new_body}.
    std::pair<Prefix, smt::Term> make(const Prefix& in_prefix,
                                       const smt::Term& formula);

    const std::unordered_map<smt::Term, smt::Term>&
    definitions() const { return _definitions; }

protected:
    smt::Term visit_node(const smt::Term& t) override;

private:
    HasUninterpreted _hu;
    std::unordered_map<smt::Term, smt::Term> _definitions;

    smt::Term mk_def(const smt::Term& t);
};
