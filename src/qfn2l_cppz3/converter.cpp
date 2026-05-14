#include "converter.h"
#include <cassert>
#include <vector>

Term NNFConverter::convert(const Term& f, bool negate) { return to_nnf(f, negate); }

Term NNFConverter::to_nnf(const Term& f, bool negate) {
    auto key = std::make_pair(f, negate);
    auto it  = _cache.find(key);
    if (it != _cache.end()) return it->second;
    Term res = to_nnf_inner(f, negate);
    _cache.insert_or_assign(key, res);
    return res;
}

Term NNFConverter::flip_cmp(const Term& t) {
    assert(t.num_args() == 2);
    Term a = t.arg(0), b = t.arg(1);
    switch (t.decl().decl_kind()) {
    case Z3_OP_LE: return (a >  b);
    case Z3_OP_GE: return (a <  b);
    case Z3_OP_LT: return (a >= b);
    case Z3_OP_GT: return (a <= b);
    default: assert(false); return t;
    }
}

Term NNFConverter::unchained(const Term& t) {
    // Break chained a op b op c into (a op b) AND (b op c).
    TermVec chs = get_children(t);
    if (chs.size() <= 2) return t;
    z3::func_decl f = t.decl();
    TermVec conjs;
    for (size_t i = 0; i + 1 < chs.size(); ++i) {
        z3::expr_vector v(_ctx.zctx);
        v.push_back(chs[i]);
        v.push_back(chs[i + 1]);
        conjs.push_back(f(v));
    }
    return mk_and(_ctx, conjs);
}

Term NNFConverter::to_nnf_inner(const Term& expr, bool negate) {
    // distinct -> not-equalities
    if (is_distinct(expr)) {
        TermVec chs = get_children(expr);
        TermVec neqs;
        for (size_t i = 0; i < chs.size(); ++i)
            for (size_t j = i + 1; j < chs.size(); ++j)
                neqs.push_back(mk_not(_ctx, chs[i] == chs[j]));
        return to_nnf(mk_and(_ctx, neqs), negate);
    }

    if (is_not(expr)) return to_nnf(expr.arg(0), !negate);

    if (is_and(expr)) {
        TermVec nc;
        for (unsigned i = 0; i < expr.num_args(); ++i)
            nc.push_back(to_nnf(expr.arg(i), negate));
        return negate ? mk_or(_ctx, nc) : mk_and(_ctx, nc);
    }

    if (is_or(expr)) {
        TermVec nc;
        for (unsigned i = 0; i < expr.num_args(); ++i)
            nc.push_back(to_nnf(expr.arg(i), negate));
        return negate ? mk_and(_ctx, nc) : mk_or(_ctx, nc);
    }

    if (is_implies(expr)) {
        Term p = expr.arg(0), q = expr.arg(1);
        if (negate)
            return mk_and2(_ctx, to_nnf(p, false), to_nnf(q, true));
        else
            return mk_or2(_ctx, to_nnf(p, true), to_nnf(q, false));
    }

    // Quantifiers: instantiate bound vars with fresh constants, recurse.
    if (expr.is_quantifier()) {
        bool expr_is_forall = Z3_is_quantifier_forall(expr.ctx(), expr);
        unsigned n = Z3_get_quantifier_num_bound(expr.ctx(), expr);

        // Create fresh constants for each bound variable.
        // Z3_get_quantifier_bound_sort(c, a, i) with i=0 is the outermost bound var.
        // De Bruijn index j in the body corresponds to bound index n-1-j.
        TermVec fresh;
        fresh.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            z3::sort s(expr.ctx(),
                       Z3_get_quantifier_bound_sort(expr.ctx(), expr, i));
            Z3_symbol sym = Z3_get_quantifier_bound_name(expr.ctx(), expr, i);
            std::string prefix = std::string(Z3_get_symbol_string(expr.ctx(), sym)) + "_";
            fresh.push_back(_ctx.fresh_symbol(s, prefix));
        }
        // substitute_vars: subs[j] replaces de Bruijn j
        // de Bruijn 0 = innermost = fresh[n-1]
        z3::expr_vector subs(_ctx.zctx);
        for (int j = static_cast<int>(n) - 1; j >= 0; --j)
            subs.push_back(fresh[j]);

        z3::expr body = expr.body();
        std::vector<Z3_ast> subs_arr;
        subs_arr.reserve(n);
        for (unsigned i = 0; i < n; ++i)
            subs_arr.push_back(static_cast<Z3_ast>(subs[i]));
        z3::expr inst_body(_ctx.zctx,
            Z3_substitute_vars(_ctx.zctx, body, n, subs_arr.data()));

        NNFConverter inner(_ctx);
        Term new_body = inner.convert(inst_body, negate);

        bool result_is_forall = negate ? !expr_is_forall : expr_is_forall;
        z3::expr_vector qvars(_ctx.zctx);
        for (auto& v : fresh) qvars.push_back(v);
        return result_is_forall ? z3::forall(qvars, new_body)
                                : z3::exists(qvars, new_body);
    }

    // Chained comparisons
    auto dk = expr.decl().decl_kind();
    bool is_chainable = (dk == Z3_OP_EQ || dk == Z3_OP_LE || dk == Z3_OP_LT ||
                         dk == Z3_OP_GE || dk == Z3_OP_GT);
    if (is_chainable && num_children(expr) > 2)
        return to_nnf(unchained(expr), negate);

    // Boolean equality: (= a b) where a,b are bool
    if (is_eq(expr) && num_children(expr) == 2) {
        Term a = expr.arg(0), b = expr.arg(1);
        if (a.get_sort().is_bool()) {
            Term rewrite = mk_and2(_ctx, mk_implies(_ctx, a, b), mk_implies(_ctx, b, a));
            return to_nnf(rewrite, negate);
        }
    }

    // Atomic comparison
    if (negate && is_chainable && num_children(expr) == 2) {
        if (is_eq(expr)) {
            Term a = expr.arg(0), b = expr.arg(1);
            return (a != b); // z3 distinct
        }
        return flip_cmp(expr);
    }

    return negate ? mk_not(_ctx, expr) : expr;
}
