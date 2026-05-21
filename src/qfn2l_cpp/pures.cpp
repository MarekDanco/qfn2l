#include "pures.h"
#include <cassert>

static std::optional<smt::Term> try_get_value(const smt::SmtSolver& s,
                                              const smt::Term& t) {
    try {
        return s->get_value(t);
    } catch (...) {
        return std::nullopt;
    }
}

// ── Pures ─────────────────────────────────────────────────────────────────────
void Pures::map_t2p(const smt::Term& t, const smt::Term& p) {
    _term2pure[t] = p;
    _pure2term[p] = t;
}

smt::Term* Pures::find_p(const smt::Term& t) {
    auto it = _term2pure.find(t);
    return it != _term2pure.end() ? &it->second : nullptr;
}

smt::Term* Pures::find_t(const smt::Term& p) {
    auto it = _pure2term.find(p);
    return it != _pure2term.end() ? &it->second : nullptr;
}

const smt::Term* Pures::find_p(const smt::Term& t) const {
    auto it = _term2pure.find(t);
    return it != _term2pure.end() ? &it->second : nullptr;
}

const smt::Term* Pures::find_t(const smt::Term& p) const {
    auto it = _pure2term.find(p);
    return it != _pure2term.end() ? &it->second : nullptr;
}

const smt::Term& Pures::get_p(const smt::Term& t) const {
    auto it = _term2pure.find(t);
    assert(it != _term2pure.end());
    return it->second;
}

const smt::Term& Pures::get_t(const smt::Term& p) const {
    auto it = _pure2term.find(p);
    assert(it != _pure2term.end());
    return it->second;
}

// ── CollectPures ──────────────────────────────────────────────────────────────
CollectPures::CollectPures(const Ctx& ctx, const Pures& pures,
                           const std::unordered_map<smt::Term, smt::TermVec>& axioms)
    : _ctx(ctx), _pures(pures), _axioms(axioms) {}

void CollectPures::operator()(const smt::Term& t) { visit(t); }

void CollectPures::visit(const smt::Term& root) {
    std::vector<smt::Term> stk = {root};
    while (!stk.empty()) {
        smt::Term t = stk.back();
        stk.pop_back();
        if (!_visited.insert(t).second)
            continue;

        for (auto it = t->begin(); it != t->end(); ++it)
            stk.push_back(*it);

        const smt::Term* orig = _pures.find_t(t);
        if (!orig || collected.count(t))
            continue;

        collected.insert(t);
        if (is_idiv(*orig))
            idiv_collected.insert(t);
        if (is_mod(*orig))
            mod_collected.insert(t);
        if (is_mul(*orig))
            mul_collected.insert(t);

        // Traverse the pure's definition so pures referenced inside it
        // (e.g. e_s^2 inside (mod e_dinv2 e_s^2)) are also collected.
        stk.push_back(*orig);

        auto ax_it = _axioms.find(t);
        if (ax_it != _axioms.end())
            for (auto& ax : ax_it->second)
                stk.push_back(ax);
    }
}

// ── CheckVal ──────────────────────────────────────────────────────────────────
CheckVal::CheckVal(const Ctx& ctx, HasUninterpreted& hu, const Pures& pures,
                   const smt::SmtSolver& lia_solver)
    : _ctx(ctx), _hu(hu), _pures(pures), _lia_solver(lia_solver) {}

bool CheckVal::check(const smt::Term& formula) {
    const auto res = (*this)(formula);
    return res && is_true(_ctx, *res);
}

CheckVal::ModelFixInfo CheckVal::model_fix_info(const smt::Term& formula) {
    ModelFixInfo info;
    collect_implicant(formula, info.implicant);

    for (const auto& lit : info.implicant) {
        for (const auto& pure : pures_in(lit)) {
            if (pure_is_wrong(pure))
                info.wrong_pures.insert(pure);
        }
    }

    for (const auto& pure : info.wrong_pures)
        info.adjustable_vars[pure] = adjustable_vars_for(pure, info.implicant);

    return info;
}

std::optional<smt::Term> CheckVal::operator()(const smt::Term& t) {
    const auto it = _memo.find(t);
    if (it != _memo.end())
        return it->second;
    const auto res = visit(t);
    _memo[t] = res;
    return res;
}

std::optional<smt::Term> CheckVal::visit_purified(const smt::Term& orig,
                                                  const smt::Term& pure) {
    // Get the value of the pure from the LIA model.
    const auto pv_opt = try_get_value(_lia_solver, pure);
    if (!pv_opt)
        return std::nullopt;
    const smt::Term pv = *pv_opt;
    // Get the actual NIA value of the original term.
    const auto tv = visit(orig);
    if (!tv)
        return std::nullopt;
    return (pv == *tv) ? std::optional<smt::Term>{pv} : std::nullopt;
}

std::optional<smt::Term> CheckVal::visit_leaf(const smt::Term& t) {
    if (t->is_value())
        return t;
    if (is_symbolic_const(t))
        return try_get_value(_lia_solver, t);
    return std::nullopt;
}

std::optional<smt::Term>
CheckVal::visit_prop(const smt::Term& t,
                     const std::vector<std::optional<smt::Term>>& cvs) {
    const auto has_none = [&] {
        for (auto& v : cvs)
            if (!v)
                return true;
        return false;
    };
    const auto in_cvs = [&](const smt::Term& x) {
        for (auto& v : cvs)
            if (v && *v == x)
                return true;
        return false;
    };

    if (is_not(t)) {
        if (!cvs[0])
            return std::nullopt;
        if (is_true(_ctx, *cvs[0]))
            return _ctx.FALSE_T;
        if (is_false(_ctx, *cvs[0]))
            return _ctx.TRUE_T;
        return std::nullopt;
    }
    if (is_or(t)) {
        if (in_cvs(_ctx.TRUE_T))
            return _ctx.TRUE_T;
        if (has_none())
            return std::nullopt;
        return _ctx.FALSE_T;
    }
    if (is_and(t)) {
        if (in_cvs(_ctx.FALSE_T))
            return _ctx.FALSE_T;
        if (has_none())
            return std::nullopt;
        return _ctx.TRUE_T;
    }
    if (is_eq(t) && cvs.size() == 2) {
        if (!cvs[0] || !cvs[1])
            return std::nullopt;
        return (*cvs[0] == *cvs[1]) ? _ctx.TRUE_T : _ctx.FALSE_T;
    }
    return std::nullopt;
}

std::optional<smt::Term>
CheckVal::visit_complex(const smt::Term& t,
                        const std::vector<std::optional<smt::Term>>& cvs) {
    bool has_none = false;
    for (auto& v : cvs)
        if (!v) {
            has_none = true;
            break;
        }

    // Bool subtree.
    if (t->get_sort() == _ctx.bool_sort)
        return visit_prop(t, cvs);

    // Numeric shortcuts.
    if (is_mul(t)) {
        for (auto& v : cvs)
            if (v && is_zero(_ctx, *v))
                return _ctx.ZERO;
    }
    if (is_mod(t) && cvs.size() == 2 && cvs[1] && is_one(_ctx, *cvs[1]))
        return _ctx.ZERO;
    if (has_none)
        return std::nullopt;
    // Division by zero is undefined.
    if ((is_mod(t) || is_idiv(t)) && cvs.size() == 2 && is_zero(_ctx, *cvs[1]))
        return std::nullopt;

    // All children have values — evaluate via the solver.
    // TODO: rebuild with concrete child values and call solver->get_value or
    // solver->simplify.  For now, use get_value on the original term.
    return try_get_value(_lia_solver, t);
}

std::optional<smt::Term> CheckVal::visit(const smt::Term& t) {
    // If it's a pure constant, check via visit_purified.
    const smt::Term* orig = _pures.find_t(t);
    if (orig)
        return visit_purified(*orig, t);

    if (t->begin() == t->end())
        return visit_leaf(t);

    // Recurse into children.
    std::vector<std::optional<smt::Term>> cvs;
    for (auto it = t->begin(); it != t->end(); ++it)
        cvs.push_back((*this)(*it));

    return visit_complex(t, cvs);
}

std::optional<smt::Term> CheckVal::model_value(const smt::Term& t) const {
    return try_get_value(_lia_solver, t);
}

bool CheckVal::collect_implicant(const smt::Term& formula, smt::TermVec& out) const {
    const auto val = model_value(formula);
    if (!val || !is_true(_ctx, *val))
        return false;

    if (is_and(formula)) {
        bool ok = true;
        for (auto it = formula->begin(); it != formula->end(); ++it)
            ok = collect_implicant(*it, out) && ok;
        return ok;
    }

    if (is_or(formula)) {
        for (auto it = formula->begin(); it != formula->end(); ++it) {
            const auto child_val = model_value(*it);
            if (child_val && is_true(_ctx, *child_val))
                return collect_implicant(*it, out);
        }
        return false;
    }

    out.push_back(formula);
    return true;
}

smt::UnorderedTermSet CheckVal::pures_in(const smt::Term& root) const {
    smt::UnorderedTermSet res, visited;
    std::vector<smt::Term> stk = {root};
    while (!stk.empty()) {
        smt::Term t = stk.back();
        stk.pop_back();
        if (!visited.insert(t).second)
            continue;
        if (_pures.find_t(t))
            res.insert(t);
        for (auto it = t->begin(); it != t->end(); ++it)
            stk.push_back(*it);
    }
    return res;
}

bool CheckVal::pure_is_wrong(const smt::Term& pure) {
    const smt::Term* orig = _pures.find_t(pure);
    if (!orig)
        return false;
    const auto pv = try_get_value(_lia_solver, pure);
    const auto tv = visit(*orig);
    return pv && tv && *pv != *tv;
}

bool CheckVal::contains_var_expanded(const smt::Term& root, const smt::Term& var,
                                     const smt::Term& skip_pure) const {
    smt::UnorderedTermSet visited;
    std::vector<smt::Term> stk = {root};
    while (!stk.empty()) {
        smt::Term t = stk.back();
        stk.pop_back();
        if (t == skip_pure)
            continue;
        if (!visited.insert(t).second)
            continue;
        if (t == var)
            return true;

        if (const smt::Term* orig = _pures.find_t(t)) {
            stk.push_back(*orig);
            continue;
        }

        for (auto it = t->begin(); it != t->end(); ++it)
            stk.push_back(*it);
    }
    return false;
}

smt::TermVec CheckVal::adjustable_vars_for(const smt::Term& pure,
                                           const smt::TermVec& implicant) const {
    const smt::Term* orig = _pures.find_t(pure);
    if (!orig)
        return {};

    smt::TermVec res;
    smt::UnorderedTermSet added;
    for (const auto& var : get_vars(*orig)) {
        if (_pures.find_t(var))
            continue;

        bool elsewhere = false;
        for (const auto& lit : implicant) {
            if (contains_var_expanded(lit, var, pure)) {
                elsewhere = true;
                break;
            }
        }
        if (!elsewhere && added.insert(var).second)
            res.push_back(var);
    }
    return res;
}
