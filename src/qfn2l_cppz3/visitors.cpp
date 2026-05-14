#include "visitors.h"
#include <algorithm>
#include <unordered_map>

// ── TermTransformer ───────────────────────────────────────────────────────────
// Iterative post-order traversal so visit_node() sees fully-transformed children.
Term TermTransformer::operator()(const Term& root) {
    {
        auto it = _memo.find(root);
        if (it != _memo.end()) return it->second;
    }
    std::vector<std::pair<Term, bool>> stk;
    stk.push_back({root, false});
    while (!stk.empty()) {
        Term t     = stk.back().first;
        bool pushed = stk.back().second;
        if (_memo.count(t)) {
            stk.pop_back();
            continue;
        }
        if (!pushed) {
            stk.back().second = true;
            if (t.is_app()) {
                for (unsigned i = 0; i < t.num_args(); ++i)
                    if (!_memo.count(t.arg(i)))
                        stk.push_back({t.arg(i), false});
            }
        } else {
            stk.pop_back();
            _memo.insert_or_assign(t, visit_node(t));
        }
    }
    return _memo.at(root);
}

Term TermTransformer::recurse(const Term& t) {
    if (!t.is_app() || t.num_args() == 0) return t;
    TermVec new_args;
    bool changed = false;
    for (unsigned i = 0; i < t.num_args(); ++i) {
        Term nc = (*this)(t.arg(i));
        if (nc.id() != t.arg(i).id()) changed = true;
        new_args.push_back(nc);
    }
    if (!changed) return t;
    return rebuild(_ctx, t, new_args);
}

// ── FlattenMul ────────────────────────────────────────────────────────────────
Term FlattenMul::visit_node(const Term& t) {
    Term t2 = recurse(t);
    if (!is_mul(t2)) return t2;
    TermVec flat;
    bool has_mul_child = false;
    for (unsigned i = 0; i < t2.num_args(); ++i) {
        Term c = t2.arg(i);
        if (is_mul(c)) {
            has_mul_child = true;
            for (unsigned j = 0; j < c.num_args(); ++j)
                flat.push_back(c.arg(j));
        } else {
            flat.push_back(c);
        }
    }
    return has_mul_child ? mk_mul(_ctx, flat) : t2;
}

// ── TermPredicate ─────────────────────────────────────────────────────────────
bool TermPredicate::operator()(const Term& root) {
    {
        auto it = _memo.find(root);
        if (it != _memo.end()) return it->second;
    }
    std::vector<std::pair<Term, bool>> stk;
    stk.push_back({root, false});
    while (!stk.empty()) {
        Term t     = stk.back().first;
        bool pushed = stk.back().second;
        if (_memo.count(t)) {
            stk.pop_back();
            continue;
        }
        if (!pushed) {
            stk.back().second = true;
            for (unsigned i = 0; i < t.num_args(); ++i)
                if (!_memo.count(t.arg(i)))
                    stk.push_back({t.arg(i), false});
        } else {
            stk.pop_back();
            _memo[t] = visit_node(t);
        }
    }
    return _memo.at(root);
}

// ── HasUninterpreted ──────────────────────────────────────────────────────────
bool HasUninterpreted::visit_node(const Term& t) {
    if (is_symbolic_const(t)) return true;
    for (unsigned i = 0; i < t.num_args(); ++i)
        if ((*this)(t.arg(i))) return true;
    return false;
}

// ── Contains ─────────────────────────────────────────────────────────────────
bool Contains::visit_node(const Term& t) {
    if (_cs.count(t)) return true;
    for (unsigned i = 0; i < t.num_args(); ++i)
        if ((*this)(t.arg(i))) return true;
    return false;
}

// ── SimpleSimplify ────────────────────────────────────────────────────────────
Term SimpleSimplify::visit_add(const Term& t) {
    std::vector<Term> stack = get_children(t);
    TermVec coeffs, others;
    while (!stack.empty()) {
        auto c = stack.back(); stack.pop_back();
        if (is_value(c))
            coeffs.push_back(c);
        else if (is_add(c))
            for (unsigned i = 0; i < c.num_args(); ++i) stack.push_back(c.arg(i));
        else
            others.push_back(c);
    }
    Term c = eval_sum(_ctx, coeffs);
    std::sort(others.begin(), others.end(),
              [](auto& a, auto& b) { return a.hash() < b.hash(); });
    if (is_zero(_ctx, c)) return mk_add(_ctx, others);
    TermVec all;
    all.push_back(c);
    all.insert(all.end(), others.begin(), others.end());
    return mk_add(_ctx, all);
}

Term SimpleSimplify::visit_mul(const Term& t) {
    std::vector<Term> stack = get_children(t);
    TermVec coeffs, others;
    while (!stack.empty()) {
        auto c = stack.back(); stack.pop_back();
        if (is_one(_ctx, c))       continue;
        if (is_zero(_ctx, c))      return _ctx.ZERO;
        if (is_value(c))           coeffs.push_back(c);
        else if (is_mul(c))
            for (unsigned i = 0; i < c.num_args(); ++i) stack.push_back(c.arg(i));
        else
            others.push_back(c);
    }
    Term c = eval_mul(_ctx, coeffs);
    if (is_zero(_ctx, c)) return _ctx.ZERO;
    std::sort(others.begin(), others.end(),
              [](auto& a, auto& b) { return a.hash() < b.hash(); });
    if (is_one(_ctx, c)) return mk_mul(_ctx, others);
    TermVec all;
    all.push_back(c);
    all.insert(all.end(), others.begin(), others.end());
    return mk_mul(_ctx, all);
}

Term SimpleSimplify::visit_or(const Term& t) {
    std::vector<Term> stack = get_children(t);
    TermVec nchs;
    while (!stack.empty()) {
        auto c = stack.back(); stack.pop_back();
        if (is_true(_ctx, c))  return _ctx.TRUE_T;
        if (is_or(c))
            for (unsigned i = 0; i < c.num_args(); ++i) stack.push_back(c.arg(i));
        else if (!is_false(_ctx, c))
            nchs.push_back(c);
    }
    std::sort(nchs.begin(), nchs.end(),
              [](auto& a, auto& b) { return a.hash() < b.hash(); });
    return mk_or(_ctx, nchs);
}

Term SimpleSimplify::visit_and(const Term& t) {
    std::vector<Term> stack = get_children(t);
    TermVec nchs;
    while (!stack.empty()) {
        auto c = stack.back(); stack.pop_back();
        if (is_false(_ctx, c)) return _ctx.FALSE_T;
        if (is_and(c))
            for (unsigned i = 0; i < c.num_args(); ++i) stack.push_back(c.arg(i));
        else if (!is_true(_ctx, c))
            nchs.push_back(c);
    }
    std::sort(nchs.begin(), nchs.end(),
              [](auto& a, auto& b) { return a.hash() < b.hash(); });
    return mk_and(_ctx, nchs);
}

Term SimpleSimplify::visit_sub(const Term& t) {
    auto chs = get_children(t);
    if (chs.size() == 2 && is_zero(_ctx, chs[1])) return chs[0];
    return t;
}

Term SimpleSimplify::visit_ite(const Term& t) {
    auto chs = get_children(t);
    assert(chs.size() == 3);
    if (is_true(_ctx, chs[0]))  return chs[1];
    if (is_false(_ctx, chs[0])) return chs[2];
    return t;
}

Term SimpleSimplify::visit_node(const Term& t) {
    Term rv = recurse(t);
    if (is_mul(rv)) return visit_mul(rv);
    if (is_add(rv)) return visit_add(rv);
    if (is_ite(rv)) return visit_ite(rv);
    if (is_sub(rv)) return visit_sub(rv);
    if (is_and(rv)) return visit_and(rv);
    if (is_or(rv))  return visit_or(rv);
    return rv;
}

// ── SimplePropagate ───────────────────────────────────────────────────────────
Term SimplePropagate::propagate(bool pos, const Term& node) {
    TermVec chs = get_children(node);
    using EqPair = std::pair<Term, Term>;
    std::vector<EqPair> eqs;

    for (int i = static_cast<int>(chs.size()) - 1; i >= 0; --i) {
        Term ch = chs[i];
        if (!pos) {
            if (!is_not(ch)) continue;
            ch = ch.arg(0);
        }
        if (!is_eq(ch)) continue;
        auto lhs = ch.arg(0), rhs = ch.arg(1);
        if (!is_symbolic_const(lhs) && !is_symbolic_const(rhs)) continue;
        if (!is_symbolic_const(lhs)) std::swap(lhs, rhs);
        if (!is_symbolic_const(lhs)) continue;
        eqs.push_back({lhs, rhs});
        chs[i] = chs.back();
        chs.pop_back();
    }
    if (eqs.empty()) return node;

    TermMap subst;
    for (auto& [lhs, rhs] : eqs) {
        Term new_rhs = do_substitute(_ctx, rhs, subst);
        subst.insert_or_assign(lhs, new_rhs);
    }
    for (auto& ch : chs)
        ch = do_substitute(_ctx, ch, subst);

    TermVec eq_lits;
    for (auto& [lhs, rhs] : subst) {
        if (z3::eq(lhs, rhs)) continue;
        Term eq = (lhs == rhs);
        eq_lits.push_back(pos ? eq : mk_not(_ctx, eq));
    }
    TermVec all_chs;
    all_chs.insert(all_chs.end(), chs.begin(), chs.end());
    all_chs.insert(all_chs.end(), eq_lits.begin(), eq_lits.end());
    std::sort(all_chs.begin(), all_chs.end(),
              [](auto& a, auto& b) { return a.hash() < b.hash(); });
    return pos ? mk_and(_ctx, all_chs) : mk_or(_ctx, all_chs);
}

Term SimplePropagate::visit_node(const Term& t) {
    Term rv = recurse(t);
    if (is_and(rv)) return propagate(true,  rv);
    if (is_or(rv))  return propagate(false, rv);
    return rv;
}

// ── MakeDefs ──────────────────────────────────────────────────────────────────
MakeDefs::MakeDefs(const Ctx& ctx) : TermTransformer(ctx), _hu(ctx) {}

Term MakeDefs::mk_def(const Term& t) {
    auto it = _definitions.find(t);
    if (it != _definitions.end()) return it->second;
    Term nc = _ctx.fresh_symbol(t.get_sort());
    _definitions.insert_or_assign(t, nc);
    return nc;
}

Term MakeDefs::visit_node(const Term& init_t) {
    Term t = recurse(init_t);
    if (!is_mul(t)) return t;

    TermVec children = get_children(t);
    int usymbols = 0;
    for (auto& c : children)
        if (_hu(c)) ++usymbols;
    if (usymbols < 2) return t;

    TermVec coeffs;
    std::unordered_map<Term, TermVec, ExprHash, ExprEq> splits;
    std::function<void(const Term&)> add = [&](const Term& c) {
        if (!_hu(c)) {
            coeffs.push_back(c);
        } else if (is_symbol(c)) {
            splits[c].push_back(c);
        } else if (is_mul(c)) {
            for (unsigned i = 0; i < c.num_args(); ++i) add(c.arg(i));
        } else {
            Term x = mk_def(c);
            splits[x].push_back(x);
        }
    };
    for (auto& c : children) add(c);

    Term coeff = eval_mul(_ctx, coeffs);
    if (is_zero(_ctx, coeff)) return _ctx.ZERO;

    std::vector<TermVec> split_list;
    for (auto& [_, v] : splits) split_list.push_back(v);

    while (split_list.size() > 2) {
        auto a = split_list[0]; split_list.erase(split_list.begin());
        auto b = split_list[0]; split_list.erase(split_list.begin());
        TermVec ab;
        ab.insert(ab.end(), a.begin(), a.end());
        ab.insert(ab.end(), b.begin(), b.end());
        Term d = mk_def(mk_mul(_ctx, ab));
        split_list.push_back({d});
    }

    assert(!split_list.empty() && split_list.size() <= 2);
    TermVec result_factors = {coeff};
    for (auto& grp : split_list)
        result_factors.insert(result_factors.end(), grp.begin(), grp.end());
    return mk_mul(_ctx, result_factors);
}

std::pair<Prefix, Term> MakeDefs::make(const Prefix& in_prefix, const Term& formula) {
    Term new_formula = (*this)(formula);
    Prefix prefix = in_prefix;
    for (auto& [t, v] : _definitions)
        prefix[0].add_var(v);

    TermVec body_parts = {new_formula};
    for (auto& [t, v] : _definitions)
        body_parts.push_back(t == v);
    return {prefix, mk_and(_ctx, body_parts)};
}
