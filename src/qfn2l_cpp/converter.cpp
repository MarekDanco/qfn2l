#include "converter.h"
#include <cassert>

smt::Term NNFConverter::convert(const smt::Term& f, bool negate) {
    return to_nnf(f, negate);
}

smt::Term NNFConverter::to_nnf(const smt::Term& f, bool negate) {
    auto key = std::make_pair(f, negate);
    auto it = _cache.find(key);
    if (it != _cache.end())
        return it->second;
    smt::Term res = to_nnf_inner(f, negate);
    _cache[key] = res;
    return res;
}

smt::Term NNFConverter::flip_cmp(const smt::Term& t) {
    auto chs = get_children(t);
    assert(chs.size() == 2);
    auto a = chs[0], b = chs[1];
    auto op = t->get_op().prim_op;
    switch (op) {
    case smt::Le:
        return _ctx.solver->make_term(smt::Gt, a, b);
    case smt::Ge:
        return _ctx.solver->make_term(smt::Lt, a, b);
    case smt::Lt:
        return _ctx.solver->make_term(smt::Ge, a, b);
    case smt::Gt:
        return _ctx.solver->make_term(smt::Le, a, b);
    default:
        assert(false);
        return t;
    }
}

smt::Term NNFConverter::unchained(const smt::Term& t) {
    // Break chained comparison a op b op c -> (a op b) AND (b op c).
    auto chs = get_children(t);
    if (chs.size() <= 2)
        return t;
    auto op = t->get_op();
    smt::TermVec conjs;
    for (size_t i = 0; i + 1 < chs.size(); ++i)
        conjs.push_back(_ctx.solver->make_term(op, chs[i], chs[i + 1]));
    return mk_and(_ctx, conjs);
}

smt::Term NNFConverter::to_nnf_inner(const smt::Term& expr, bool negate) {
    // distinct -> not-equalities
    if (is_distinct(expr)) {
        auto chs = get_children(expr);
        smt::TermVec neqs;
        for (size_t i = 0; i < chs.size(); ++i)
            for (size_t j = i + 1; j < chs.size(); ++j)
                neqs.push_back(
                    mk_not(_ctx, _ctx.solver->make_term(smt::Equal, chs[i], chs[j])));
        return to_nnf(mk_and(_ctx, neqs), negate);
    }

    if (is_not(expr))
        return to_nnf(get_child(expr, 0), !negate);

    if (is_and(expr)) {
        smt::TermVec nc;
        for (auto it = expr->begin(); it != expr->end(); ++it)
            nc.push_back(to_nnf(*it, negate));
        return negate ? mk_or(_ctx, nc) : mk_and(_ctx, nc);
    }

    if (is_or(expr)) {
        smt::TermVec nc;
        for (auto it = expr->begin(); it != expr->end(); ++it)
            nc.push_back(to_nnf(*it, negate));
        return negate ? mk_and(_ctx, nc) : mk_or(_ctx, nc);
    }

    if (is_implies(expr)) {
        auto p = get_child(expr, 0), q = get_child(expr, 1);
        if (negate)
            return mk_and2(_ctx, to_nnf(p, false), to_nnf(q, true));
        else
            return mk_or2(_ctx, to_nnf(p, true), to_nnf(q, false));
    }

    // Quantifiers: replace bound vars with fresh constants, recurse.
    // TODO: smt-switch quantifier introspection API may differ.
    // The approach below uses get_children to get [var1, var2, ..., body].
    if (is_forall(expr) || is_exists(expr)) {
        bool expr_is_forall = is_forall(expr);
        auto all_chs = get_children(expr);
        assert(!all_chs.empty());
        smt::Term body = all_chs.back();
        smt::TermVec bound_vars(all_chs.begin(), all_chs.end() - 1);

        // Create fresh symbols for bound vars and substitute.
        smt::TermVec fresh;
        for (auto& v : bound_vars)
            fresh.push_back(_ctx.fresh_symbol(v->get_sort(), v->to_string() + "_"));

        NNFConverter inner(_ctx);
        smt::Term new_body =
            inner.convert(do_substitute(_ctx, body, bound_vars, fresh), negate);

        bool result_is_forall = negate ? !expr_is_forall : expr_is_forall;
        smt::PrimOp qop = result_is_forall ? smt::Forall : smt::Exists;
        smt::TermVec qargs = fresh;
        qargs.push_back(new_body);
        return _ctx.solver->make_term(qop, qargs);
    }

    // Chained comparisons: a <= b <= c has >2 children for some backends.
    auto op = expr->get_op().prim_op;
    bool is_chainable = (op == smt::Equal || op == smt::Le || op == smt::Lt ||
                         op == smt::Ge || op == smt::Gt);
    if (is_chainable && num_children(expr) > 2)
        return to_nnf(unchained(expr), negate);

    // Boolean equality: (= a b) where a,b are bool -> (a => b) AND (b => a)
    if (is_eq(expr) && num_children(expr) == 2) {
        auto a = get_child(expr, 0), b = get_child(expr, 1);
        if (a->get_sort() == _ctx.bool_sort) {
            smt::Term rewrite =
                mk_and2(_ctx, mk_implies(_ctx, a, b), mk_implies(_ctx, b, a));
            return to_nnf(rewrite, negate);
        }
    }

    // Atomic comparison — just flip if negating.
    if (negate && is_chainable && num_children(expr) == 2) {
        if (is_eq(expr)) {
            auto a = get_child(expr, 0), b = get_child(expr, 1);
            return _ctx.solver->make_term(smt::Distinct, a, b);
        }
        return flip_cmp(expr);
    }

    return negate ? mk_not(_ctx, expr) : expr;
}
