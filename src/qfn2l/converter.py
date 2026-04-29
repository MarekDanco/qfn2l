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

    def _to_nnf(self, root: ExprRef, root_negate: bool) -> ExprRef:
        if (root, root_negate) in self._nnf_cache:
            return self._nnf_cache[(root, root_negate)]
        stack = [(root, root_negate)]
        while stack:
            expr, negate = stack[-1]
            key = (expr, negate)
            if key in self._nnf_cache:
                stack.pop()
                continue

            if is_not(expr):
                inner_key = (expr.arg(0), not negate)
                if inner_key not in self._nnf_cache:
                    stack.append((expr.arg(0), not negate))
                else:
                    stack.pop()
                    self._nnf_cache[key] = self._nnf_cache[inner_key]

            elif is_and(expr) or is_or(expr):
                children = expr.children()
                pending = [(c, negate) for c in children
                           if (c, negate) not in self._nnf_cache]
                if pending:
                    stack.extend(pending)
                else:
                    stack.pop()
                    converted = [self._nnf_cache[(c, negate)] for c in children]
                    if is_and(expr):
                        self._nnf_cache[key] = Or(converted) if negate else And(converted)
                    else:
                        self._nnf_cache[key] = And(converted) if negate else Or(converted)

            elif is_implies(expr):
                a, b = expr.arg(0), expr.arg(1)
                ak = (a, not negate)
                bk = (b, negate)
                pending = [k for k in [ak, bk] if k not in self._nnf_cache]
                if pending:
                    stack.extend(pending)
                else:
                    stack.pop()
                    ac, bc = self._nnf_cache[ak], self._nnf_cache[bk]
                    self._nnf_cache[key] = And(ac, bc) if negate else Or(ac, bc)

            elif is_distinct(expr):
                rw = distinct_as_not_equalities(expr)
                rk = (rw, negate)
                if rk not in self._nnf_cache:
                    stack.append((rw, negate))
                else:
                    stack.pop()
                    self._nnf_cache[key] = self._nnf_cache[rk]

            elif expr.num_args() > 2 and is_chainable(expr):
                rw = mk_chainable(expr.decl(), expr.children())
                rk = (rw, negate)
                if rk not in self._nnf_cache:
                    stack.append((rw, negate))
                else:
                    stack.pop()
                    self._nnf_cache[key] = self._nnf_cache[rk]

            elif is_eq(expr) and is_bool(expr.arg(0)):
                c0, c1 = expr.arg(0), expr.arg(1)
                rw = And(Implies(c0, c1), Implies(c1, c0))
                rk = (rw, negate)
                if rk not in self._nnf_cache:
                    stack.append((rw, negate))
                else:
                    stack.pop()
                    self._nnf_cache[key] = self._nnf_cache[rk]

            elif is_quantifier(expr):
                assert isinstance(expr, QuantifierRef)
                stack.pop()
                num_vars = expr.num_vars()
                bound_vars = [
                    Const(expr.var_name(i), expr.var_sort(i)) for i in range(num_vars)
                ]
                body_with_consts = substitute_vars(expr.body(), *reversed(bound_vars))
                new_body = NNFConverter().convert(body_with_consts, negate=negate)
                if negate:
                    result = Exists(bound_vars, new_body) if expr.is_forall() else ForAll(bound_vars, new_body)
                else:
                    result = ForAll(bound_vars, new_body) if expr.is_forall() else Exists(bound_vars, new_body)
                self._nnf_cache[key] = result

            else:
                stack.pop()
                if is_le(expr):
                    result = mk_binary(op.gt if negate else op.le, expr.children())
                elif is_ge(expr):
                    result = mk_binary(op.lt if negate else op.ge, expr.children())
                elif is_gt(expr):
                    result = mk_binary(op.le if negate else op.gt, expr.children())
                elif is_lt(expr):
                    result = mk_binary(op.ge if negate else op.lt, expr.children())
                else:
                    result = Not(expr) if negate else expr
                self._nnf_cache[key] = result

        return self._nnf_cache[(root, root_negate)]
