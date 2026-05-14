#include "visitors.h"
#include <algorithm>
#include <unordered_map>

// ── TermTransformer ───────────────────────────────────────────────────────────
// Iterative post-order traversal: processes children before parents so that
// visit_node() always sees its children already memoized.  visit_node() may
// still call (*this) on freshly-built terms, but those complete in O(1) since
// their children are already in _memo.
smt::Term TermTransformer::operator()(const smt::Term& root) {
    {
        auto it = _memo.find(root);
        if (it != _memo.end())
            return it->second;
    }
    // Stack entries: (term, children_already_pushed)
    std::vector<std::pair<smt::Term, bool>> stk;
    stk.push_back({root, false});
    while (!stk.empty()) {
        smt::Term t = stk.back().first; // copy before possible realloc
        bool pushed = stk.back().second;
        if (_memo.count(t)) {
            stk.pop_back();
            continue;
        }
        if (!pushed) {
            stk.back().second = true; // mark before push_back may reallocate
            if (is_app(t)) {
                for (auto it = t->begin(); it != t->end(); ++it)
                    if (!_memo.count(*it))
                        stk.push_back({*it, false});
            }
        } else {
            stk.pop_back();
            // All children are in _memo; visit_node calls to (*this) are O(1).
            _memo[t] = visit_node(t);
        }
    }
    return _memo.at(root);
}

smt::Term TermTransformer::recurse(const smt::Term& t) {
    if (!is_app(t) || t->begin() == t->end())
        return t;
    smt::TermVec new_args;
    bool changed = false;
    for (auto it = t->begin(); it != t->end(); ++it) {
        smt::Term nc = (*this)(*it);
        if (nc != *it)
            changed = true;
        new_args.push_back(nc);
    }
    if (!changed) return t;
    return rebuild(_ctx, t, new_args);
}

// ── FlattenMul ────────────────────────────────────────────────────────────────
smt::Term FlattenMul::visit_node(const smt::Term& t) {
    smt::Term t2 = recurse(t);
    if (!is_mul(t2))
        return t2;
    smt::TermVec flat;
    bool has_mul_child = false;
    for (auto it = t2->begin(); it != t2->end(); ++it) {
        if (is_mul(*it)) {
            has_mul_child = true;
            for (auto jt = (*it)->begin(); jt != (*it)->end(); ++jt)
                flat.push_back(*jt);
        } else {
            flat.push_back(*it);
        }
    }
    return has_mul_child ? mk_mul(_ctx, flat) : t2;
}

// ── TermPredicate ─────────────────────────────────────────────────────────────
bool TermPredicate::operator()(const smt::Term& root) {
    {
        auto it = _memo.find(root);
        if (it != _memo.end())
            return it->second;
    }
    std::vector<std::pair<smt::Term, bool>> stk;
    stk.push_back({root, false});
    while (!stk.empty()) {
        smt::Term t = stk.back().first;
        bool pushed = stk.back().second;
        if (_memo.count(t)) {
            stk.pop_back();
            continue;
        }
        if (!pushed) {
            stk.back().second = true;
            for (auto it = t->begin(); it != t->end(); ++it)
                if (!_memo.count(*it))
                    stk.push_back({*it, false});
        } else {
            stk.pop_back();
            _memo[t] = visit_node(t);
        }
    }
    return _memo.at(root);
}

// ── HasUninterpreted ──────────────────────────────────────────────────────────
bool HasUninterpreted::visit_node(const smt::Term& t) {
    if (is_symbolic_const(t))
        return true;
    for (auto it = t->begin(); it != t->end(); ++it)
        if ((*this)(*it))
            return true;
    return false;
}

// ── Contains ─────────────────────────────────────────────────────────────────
bool Contains::visit_node(const smt::Term& t) {
    if (_cs.count(t))
        return true;
    for (auto it = t->begin(); it != t->end(); ++it)
        if ((*this)(*it))
            return true;
    return false;
}

// ── SimpleSimplify ────────────────────────────────────────────────────────────
smt::Term SimpleSimplify::visit_add(const smt::Term& t) {
    // Flatten and collect numerals.
    std::vector<smt::Term> stack = get_children(t);
    smt::TermVec coeffs, others;
    while (!stack.empty()) {
        auto c = stack.back();
        stack.pop_back();
        if (is_value(c))
            coeffs.push_back(c);
        else if (is_add(c))
            for (auto ch : get_children(c))
                stack.push_back(ch);
        else
            others.push_back(c);
    }
    smt::Term c = eval_sum(_ctx, coeffs);
    std::sort(others.begin(), others.end(),
              [](auto& a, auto& b) { return a->hash() < b->hash(); });
    if (is_zero(_ctx, c))
        return mk_add(_ctx, others);
    smt::TermVec all;
    all.push_back(c);
    all.insert(all.end(), others.begin(), others.end());
    return mk_add(_ctx, all);
}

smt::Term SimpleSimplify::visit_mul(const smt::Term& t) {
    std::vector<smt::Term> stack = get_children(t);
    smt::TermVec coeffs, others;
    while (!stack.empty()) {
        auto c = stack.back();
        stack.pop_back();
        if (is_one(_ctx, c))
            continue;
        if (is_zero(_ctx, c))
            return _ctx.ZERO;
        if (is_value(c))
            coeffs.push_back(c);
        else if (is_mul(c))
            for (auto ch : get_children(c))
                stack.push_back(ch);
        else
            others.push_back(c);
    }
    smt::Term c = eval_mul(_ctx, coeffs);
    if (is_zero(_ctx, c))
        return _ctx.ZERO;
    std::sort(others.begin(), others.end(),
              [](auto& a, auto& b) { return a->hash() < b->hash(); });
    if (is_one(_ctx, c))
        return mk_mul(_ctx, others);
    smt::TermVec all;
    all.push_back(c);
    all.insert(all.end(), others.begin(), others.end());
    return mk_mul(_ctx, all);
}

smt::Term SimpleSimplify::visit_or(const smt::Term& t) {
    std::vector<smt::Term> stack = get_children(t);
    smt::TermVec nchs;
    while (!stack.empty()) {
        auto c = stack.back();
        stack.pop_back();
        if (is_true(_ctx, c))
            return _ctx.TRUE_T;
        if (is_or(c))
            for (auto ch : get_children(c))
                stack.push_back(ch);
        else if (!is_false(_ctx, c))
            nchs.push_back(c);
    }
    std::sort(nchs.begin(), nchs.end(),
              [](auto& a, auto& b) { return a->hash() < b->hash(); });
    return mk_or(_ctx, nchs);
}

smt::Term SimpleSimplify::visit_and(const smt::Term& t) {
    std::vector<smt::Term> stack = get_children(t);
    smt::TermVec nchs;
    while (!stack.empty()) {
        auto c = stack.back();
        stack.pop_back();
        if (is_false(_ctx, c))
            return _ctx.FALSE_T;
        if (is_and(c))
            for (auto ch : get_children(c))
                stack.push_back(ch);
        else if (!is_true(_ctx, c))
            nchs.push_back(c);
    }
    std::sort(nchs.begin(), nchs.end(),
              [](auto& a, auto& b) { return a->hash() < b->hash(); });
    return mk_and(_ctx, nchs);
}

smt::Term SimpleSimplify::visit_sub(const smt::Term& t) {
    auto chs = get_children(t);
    if (chs.size() == 2 && is_zero(_ctx, chs[1]))
        return chs[0];
    return t;
}

smt::Term SimpleSimplify::visit_ite(const smt::Term& t) {
    auto chs = get_children(t);
    assert(chs.size() == 3);
    if (is_true(_ctx, chs[0]))
        return chs[1];
    if (is_false(_ctx, chs[0]))
        return chs[2];
    return t;
}

smt::Term SimpleSimplify::visit_node(const smt::Term& t) {
    smt::Term rv = recurse(t);
    if (is_mul(rv))
        return visit_mul(rv);
    if (is_add(rv))
        return visit_add(rv);
    if (is_ite(rv))
        return visit_ite(rv);
    if (is_sub(rv))
        return visit_sub(rv);
    if (is_and(rv))
        return visit_and(rv);
    if (is_or(rv))
        return visit_or(rv);
    // Constant-fold fully-numeral applications.
    if (is_app(rv) && rv->begin() != rv->end()) {
        bool all_vals = true;
        for (auto it = rv->begin(); it != rv->end(); ++it)
            if (!(*it)->is_value()) {
                all_vals = false;
                break;
            }
        if (all_vals) {
            // TODO: call solver->simplify(rv) or evaluate via get_value.
            // For now, return as-is; this is conservative.
        }
    }
    return rv;
}

// ── SimplePropagate ───────────────────────────────────────────────────────────
smt::Term SimplePropagate::propagate(bool pos, const smt::Term& node) {
    smt::TermVec chs = get_children(node);

    // Extract equalities of the form (= symbolic_const value_or_const).
    // Under And (pos=true): extract x==c.
    // Under Or (pos=false): extract not(x==c).
    using EqPair = std::pair<smt::Term, smt::Term>;
    std::vector<EqPair> eqs;

    for (int i = static_cast<int>(chs.size()) - 1; i >= 0; --i) {
        smt::Term ch = chs[i];
        // Under Or, unwrap Not.
        if (!pos) {
            if (!is_not(ch))
                continue;
            ch = get_child(ch, 0);
        }
        if (!is_eq(ch))
            continue;
        auto lhs = get_child(ch, 0), rhs = get_child(ch, 1);
        if (!is_symbolic_const(lhs) && !is_symbolic_const(rhs))
            continue;
        if (!is_symbolic_const(lhs))
            std::swap(lhs, rhs);
        if (!is_symbolic_const(lhs) || !is_symbolic_const(rhs))
            continue;
        eqs.push_back({lhs, rhs});
        // Swap-remove.
        chs[i] = chs.back();
        chs.pop_back();
    }
    if (eqs.empty())
        return node;

    // Build substitution, applying earlier subs to later rhs values.
    smt::UnorderedTermMap subst;
    for (auto& [lhs, rhs] : eqs) {
        smt::Term new_rhs = do_substitute(_ctx, rhs, subst);
        subst[lhs] = new_rhs;
    }

    // Apply substitution to remaining children.
    for (auto& ch : chs)
        ch = do_substitute(_ctx, ch, subst);

    // Rebuild equality literals.
    smt::TermVec eq_lits;
    for (auto& [lhs, rhs] : subst) {
        if (lhs == rhs)
            continue;
        smt::Term eq = _ctx.solver->make_term(smt::Equal, lhs, rhs);
        eq_lits.push_back(pos ? eq : mk_not(_ctx, eq));
    }
    smt::TermVec all_chs;
    all_chs.insert(all_chs.end(), chs.begin(), chs.end());
    all_chs.insert(all_chs.end(), eq_lits.begin(), eq_lits.end());
    std::sort(all_chs.begin(), all_chs.end(),
              [](auto& a, auto& b) { return a->hash() < b->hash(); });
    return pos ? mk_and(_ctx, all_chs) : mk_or(_ctx, all_chs);
}

smt::Term SimplePropagate::visit_node(const smt::Term& t) {
    smt::Term rv = recurse(t);
    if (is_and(rv))
        return propagate(true, rv);
    if (is_or(rv))
        return propagate(false, rv);
    return rv;
}

// ── MakeDefs ──────────────────────────────────────────────────────────────────
MakeDefs::MakeDefs(const Ctx& ctx) : TermTransformer(ctx), _hu(ctx) {}

smt::Term MakeDefs::mk_def(const smt::Term& t) {
    auto it = _definitions.find(t);
    if (it != _definitions.end())
        return it->second;
    smt::Term nc = _ctx.fresh_symbol(t->get_sort());
    _definitions[t] = nc;
    return nc;
}

smt::Term MakeDefs::visit_node(const smt::Term& init_t) {
    smt::Term t = recurse(init_t);
    // Don't call (*this)(t) here: t is not yet in _memo so it would recurse
    // infinitely. Children of t are already transformed by recurse(); apply
    // the mul logic directly.
    if (!is_mul(t))
        return t;

    smt::TermVec children = get_children(t);
    int usymbols = 0;
    for (auto& c : children)
        if (_hu(c))
            ++usymbols;
    if (usymbols < 2)
        return t;

    // Separate numeric coefficients from symbolic factors.
    // Inline nested mul children to avoid creating intermediate definitions
    // for powers of a single symbol (e.g. x*(x*x) -> group {x:[x,x,x]}).
    smt::TermVec coeffs;
    std::unordered_map<smt::Term, smt::TermVec> splits;
    std::function<void(const smt::Term&)> add = [&](const smt::Term& c) {
        if (!_hu(c)) {
            coeffs.push_back(c);
        } else if (is_symbol(c)) {
            splits[c].push_back(c);
        } else if (is_mul(c)) {
            for (auto it = c->begin(); it != c->end(); ++it)
                add(*it);
        } else {
            smt::Term x = mk_def(c);
            splits[x].push_back(x);
        }
    };
    for (auto& c : children)
        add(c);

    smt::Term coeff = eval_mul(_ctx, coeffs);
    if (is_zero(_ctx, coeff))
        return _ctx.ZERO;

    // Reduce: while >2 groups, merge two.
    std::vector<smt::TermVec> split_list;
    for (auto& [_, v] : splits)
        split_list.push_back(v);

    while (split_list.size() > 2) {
        auto a = split_list[0];
        split_list.erase(split_list.begin());
        auto b = split_list[0];
        split_list.erase(split_list.begin());
        smt::TermVec ab;
        ab.insert(ab.end(), a.begin(), a.end());
        ab.insert(ab.end(), b.begin(), b.end());
        smt::Term d = mk_def(mk_mul(_ctx, ab));
        split_list.push_back({d});
    }

    assert(!split_list.empty() && split_list.size() <= 2);
    smt::TermVec result_factors = {coeff};
    for (auto& grp : split_list)
        result_factors.insert(result_factors.end(), grp.begin(), grp.end());
    return mk_mul(_ctx, result_factors);
}

std::pair<Prefix, smt::Term> MakeDefs::make(const Prefix& in_prefix,
                                            const smt::Term& formula) {
    smt::Term new_formula = (*this)(formula);
    Prefix prefix = in_prefix;

    for (auto& [t, v] : _definitions)
        prefix[0].add_var(v);

    // new_body = new_formula AND (t == v for each definition)
    smt::TermVec body_parts = {new_formula};
    for (auto& [t, v] : _definitions)
        body_parts.push_back(_ctx.solver->make_term(smt::Equal, t, v));
    return {prefix, mk_and(_ctx, body_parts)};
}
