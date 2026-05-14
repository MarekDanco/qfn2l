#include "pures.h"
#include <cassert>

static std::optional<Term> try_get_value(const Ctx& ctx, const Term& t) {
    try {
        return ctx.get_value(t);
    } catch (...) {
        return std::nullopt;
    }
}

// ── Pures ─────────────────────────────────────────────────────────────────────
void Pures::map_t2p(const Term& t, const Term& p) {
    _term2pure.insert_or_assign(t, p);
    _pure2term.insert_or_assign(p, t);
}

Term* Pures::find_p(const Term& t) {
    auto it = _term2pure.find(t);
    return it != _term2pure.end() ? &it->second : nullptr;
}

Term* Pures::find_t(const Term& p) {
    auto it = _pure2term.find(p);
    return it != _pure2term.end() ? &it->second : nullptr;
}

const Term& Pures::get_p(const Term& t) const {
    auto it = _term2pure.find(t);
    assert(it != _term2pure.end());
    return it->second;
}

const Term& Pures::get_t(const Term& p) const {
    auto it = _pure2term.find(p);
    assert(it != _pure2term.end());
    return it->second;
}

// ── CollectPures ──────────────────────────────────────────────────────────────
CollectPures::CollectPures(
    const Ctx& ctx, const Pures& pures,
    const std::unordered_map<Term, TermVec, ExprHash, ExprEq>& axioms)
    : _ctx(ctx), _pures(pures), _axioms(axioms) {}

void CollectPures::operator()(const Term& t) { visit(t); }

void CollectPures::visit(const Term& root) {
    std::vector<Term> stk = {root};
    while (!stk.empty()) {
        Term t = stk.back();
        stk.pop_back();
        if (!_visited.insert(t).second) continue;

        for (unsigned i = 0; i < t.num_args(); ++i)
            stk.push_back(t.arg(i));

        const Term* orig = const_cast<Pures&>(_pures).find_t(t);
        if (!orig || collected.count(t)) continue;

        collected.insert(t);
        if (is_idiv(*orig)) idiv_collected.insert(t);
        if (is_mod(*orig))  mod_collected.insert(t);
        if (is_mul(*orig))  mul_collected.insert(t);

        auto ax_it = _axioms.find(t);
        if (ax_it != _axioms.end())
            for (auto& ax : ax_it->second)
                stk.push_back(ax);
    }
}

// ── CheckVal ──────────────────────────────────────────────────────────────────
CheckVal::CheckVal(const Ctx& ctx, HasUninterpreted& hu, const Pures& pures)
    : _ctx(ctx), _hu(hu), _pures(pures) {}

bool CheckVal::check(const Term& formula) {
    auto res = (*this)(formula);
    return res && (is_true(_ctx, *res) || z3::eq(*res, _ctx.TRUE_T));
}

std::optional<Term> CheckVal::operator()(const Term& t) {
    auto it = _memo.find(t);
    if (it != _memo.end()) return it->second;
    auto res  = visit(t);
    _memo[t] = res;
    return res;
}

std::optional<Term> CheckVal::visit_purified(const Term& orig, const Term& pure) {
    auto pv_opt = try_get_value(_ctx, pure);
    if (!pv_opt) return std::nullopt;
    Term pv = *pv_opt;
    auto tv  = visit(orig);
    if (!tv) return std::nullopt;
    return z3::eq(pv, *tv) ? std::optional<Term>{pv} : std::nullopt;
}

std::optional<Term> CheckVal::visit_leaf(const Term& t) {
    if (is_value(t))           return t;
    if (is_symbolic_const(t))  return try_get_value(_ctx, t);
    return std::nullopt;
}

std::optional<Term>
CheckVal::visit_prop(const Term& t, const std::vector<std::optional<Term>>& cvs) {
    auto has_none = [&] {
        for (auto& v : cvs) if (!v) return true;
        return false;
    };
    auto in_cvs = [&](const Term& x) {
        for (auto& v : cvs) if (v && z3::eq(*v, x)) return true;
        return false;
    };

    if (is_not(t)) {
        if (!cvs[0]) return std::nullopt;
        if (is_true(_ctx,  *cvs[0])) return _ctx.FALSE_T;
        if (is_false(_ctx, *cvs[0])) return _ctx.TRUE_T;
        return std::nullopt;
    }
    if (is_true(_ctx, t))  return _ctx.TRUE_T;
    if (is_false(_ctx, t)) return _ctx.FALSE_T;
    if (is_or(t)) {
        if (in_cvs(_ctx.TRUE_T))  return _ctx.TRUE_T;
        if (has_none())           return std::nullopt;
        return _ctx.FALSE_T;
    }
    if (is_and(t)) {
        if (in_cvs(_ctx.FALSE_T)) return _ctx.FALSE_T;
        if (has_none())           return std::nullopt;
        return _ctx.TRUE_T;
    }
    if (is_eq(t) && cvs.size() == 2) {
        if (!cvs[0] || !cvs[1]) return std::nullopt;
        return z3::eq(*cvs[0], *cvs[1]) ? _ctx.TRUE_T : _ctx.FALSE_T;
    }
    return std::nullopt;
}

std::optional<Term>
CheckVal::visit_complex(const Term& t, const std::vector<std::optional<Term>>& cvs) {
    bool has_none = false;
    for (auto& v : cvs) if (!v) { has_none = true; break; }

    if (z3::eq(t.get_sort(), _ctx.bool_sort))
        return visit_prop(t, cvs);

    if (is_mul(t)) {
        for (auto& v : cvs)
            if (v && is_zero(_ctx, *v)) return _ctx.ZERO;
    }
    if (is_mod(t) && cvs.size() == 2 && cvs[1] && is_one(_ctx, *cvs[1]))
        return _ctx.ZERO;
    if (has_none) return std::nullopt;
    if ((is_mod(t) || is_idiv(t)) && cvs.size() == 2 && is_zero(_ctx, *cvs[1]))
        return std::nullopt;

    return try_get_value(_ctx, t);
}

std::optional<Term> CheckVal::visit(const Term& t) {
    const Term* orig = const_cast<Pures&>(_pures).find_t(t);
    if (orig) return visit_purified(*orig, t);

    if (t.num_args() == 0) return visit_leaf(t);

    std::vector<std::optional<Term>> cvs;
    for (unsigned i = 0; i < t.num_args(); ++i)
        cvs.push_back((*this)(t.arg(i)));

    return visit_complex(t, cvs);
}
