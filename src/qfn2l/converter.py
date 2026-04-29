"""Various normal form converter classes."""

import operator as op

import utils
from z3 import (
    And,
    Const,
    Exists,
    ExprRef,
    ForAll,
    Implies,
    Not,
    Or,
    QuantifierRef,
    is_and,
    is_bool,
    is_distinct,
    is_eq,
    is_ge,
    is_gt,
    is_implies,
    is_le,
    is_lt,
    is_not,
    is_or,
    is_quantifier,
    substitute_vars,
)


def is_chainable(expr) -> bool:
    return is_eq(expr) or is_le(expr) or is_lt(expr) or is_ge(expr) or is_gt(expr)


def mk_binary(f, ls):
    """Apply a binary operator."""
    assert len(ls) == 2, f"we expect 2 operands {ls}"
    return f(ls[0], ls[1])


def mk_chainable(f, ls):
    """Creates an conjunction out of a chainable operator."""
    assert len(ls) >= 2, f"we expect at least 2 operands {ls}"
    res = f(ls[0], ls[1])
    for i in range(2, len(ls)):
        res = And(res, f(ls[i - 1], ls[i]))
    return res


def distinct_as_not_equalities(e: ExprRef) -> ExprRef:
    cs = e.children()
    return utils.mk_and(
        *[Not(cs[i] == cs[j]) for i in range(len(cs)) for j in range(i + 1, len(cs))]
    )


class NNFConverter:
    def __init__(self):
        self._nnf_cache = {}
        self._check_cache = {}

    def _check_correct(self, expr, seen_not):
        if expr in self._check_cache:
            return self._check_cache[expr]
        if is_quantifier(expr):
            if seen_not:
                return False
        if is_not(expr):
            seen_not = True
        res = all(self._check_correct(c, seen_not) for c in expr.children())
        self._check_cache[expr] = res
        return res

    def __call__(self, f: ExprRef):
        """Convert f to nnf."""
        return self.convert(f, check=False, negate=False)

    def convert(self, f: ExprRef, check=False, negate=False):
        """Convert f to nnf."""
        out = self._to_nnf(f, negate)
        if check:
            assert self._check_correct(out, False), (
                "NNF conversion did not go correctly."
            )
        return out

    def _to_nnf_inner(self, expr: ExprRef, negate) -> ExprRef:
        if is_distinct(expr):
            return self._to_nnf(distinct_as_not_equalities(expr), negate)
        if is_not(expr):
            return self._to_nnf(expr.arg(0), not negate)
        elif is_and(expr):
            if negate:
                return Or([self._to_nnf(child, True) for child in expr.children()])
            else:
                return And([self._to_nnf(child, False) for child in expr.children()])
        elif is_or(expr):
            if negate:
                return And([self._to_nnf(child, True) for child in expr.children()])
            else:
                return Or([self._to_nnf(child, False) for child in expr.children()])
        elif is_implies(expr):
            if negate:
                return And(
                    self._to_nnf(expr.arg(0), False), self._to_nnf(expr.arg(1), True)
                )
            else:
                return Or(
                    self._to_nnf(expr.arg(0), True), self._to_nnf(expr.arg(1), False)
                )
        elif is_quantifier(expr):
            assert isinstance(expr, QuantifierRef)
            num_vars = expr.num_vars()
            var_names = [expr.var_name(i) for i in range(num_vars)]
            var_sorts = [expr.var_sort(i) for i in range(num_vars)]

            bound_vars = [Const(var_names[i], var_sorts[i]) for i in range(num_vars)]

            body = expr.body()
            body_with_consts = substitute_vars(body, *reversed(bound_vars))

            qconverter = NNFConverter()
            new_body = qconverter.convert(body_with_consts, check=False, negate=negate)
            if negate:
                if expr.is_forall():
                    return Exists(bound_vars, new_body)
                else:
                    return ForAll(bound_vars, new_body)
            else:
                if expr.is_forall():
                    return ForAll(bound_vars, new_body)
                else:
                    return Exists(bound_vars, new_body)
        elif expr.num_args() > 2 and is_chainable(expr):
            return self._to_nnf(mk_chainable(expr.decl(), expr.children()), negate)
        elif is_eq(expr) and is_bool(expr.arg(0)):
            assert expr.num_args() == 2, f"we should have gotten rid of chaining {expr}"
            child_0 = expr.arg(0)
            child_1 = expr.arg(1)
            implies_01 = Implies(child_0, child_1)
            implies_10 = Implies(child_1, child_0)
            rewrite = And(implies_01, implies_10)
            return self._to_nnf(rewrite, negate)
        elif is_le(expr):
            return mk_binary(op.gt if negate else op.le, expr.children())
        elif is_ge(expr):
            return mk_binary(op.lt if negate else op.ge, expr.children())
        elif is_gt(expr):
            return mk_binary(op.le if negate else op.gt, expr.children())
        elif is_lt(expr):
            return mk_binary(op.ge if negate else op.lt, expr.children())
        return Not(expr) if negate else expr

    def _to_nnf(self, expr: ExprRef, negate: bool) -> ExprRef:
        cache_key = (expr, negate)
        if cache_key in self._nnf_cache:
            return self._nnf_cache[cache_key]
        res = self._to_nnf_inner(expr, negate)
        self._nnf_cache[cache_key] = res
        return res
