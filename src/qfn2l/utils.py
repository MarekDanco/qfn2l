#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Created on:  Sun Dec 7 16:22:27 CET 2025
# Copyright (C) 2025, Mikolas Janota

import typing
from collections import defaultdict

import z3
from z3 import (
    ArithRef,
    BoolRef,
    BoolVal,
    ExprRef,
    IntNumRef,
    IntVal,
    Product,
    Sum,
    is_and,
    is_false,
    is_int,
    is_int_value,
    is_mul,
    is_not,
    is_or,
    is_true,
)


def is_ite(a) -> bool:
    return z3.is_app_of(a, z3.Z3_OP_ITE)


def print_smt2_formula(formula: ExprRef) -> None:
    ps = z3.Solver()
    ps.add(formula)
    print(ps.to_smt2())


class GetLevel:
    """Calculates a maximum level over a z3 term, given access to a mapping of
    constants to their levels. It is memoized, so it's possible to get all the
    terms in a formula, if needed (terms getter).

    The call operator is overloaded to use the class. Defaults to -1 if
    the the formula does not contain a constant.
    """

    def __init__(self, const2lev, a=None):
        self._memo = {}
        self._terms = {}
        self.const2lev = const2lev
        if a is not None:
            self(a)

    def terms(self) -> dict[ExprRef, int]:
        """Terms getter."""
        return self._terms

    def __call__(self, e: ExprRef) -> int:
        """Calculates the maximum level of the given Z3 term."""
        if e in self._memo:
            return self._memo[e]

        level = self._calculate_max_level(e)
        if level >= 0 and not is_nnf_connective(e):
            self._terms[e] = level

        self._memo[e] = level
        return level

    def _calculate_max_level(self, term):
        if term.num_args() == 0:
            return self.const2lev.get(term, -1)
        max_level = 0
        for arg in term.children():
            arg_level = self(arg)
            max_level = max(max_level, arg_level)
        return max_level


ZERO = z3.IntVal(0)
ONE = z3.IntVal(1)
FALSE = BoolVal(False)
TRUE = BoolVal(True)
MIN_ONE = z3.IntVal(-1)


def negate(n: IntNumRef) -> IntNumRef:
    assert is_int_value(n)
    v = n.as_long()
    return IntVal(-v)


def is_neg(n: ExprRef) -> bool:
    assert is_int_value(n)
    n_int = typing.cast(IntNumRef, n)
    return n_int.as_long() < 0


def is_min_one(t: ExprRef) -> bool:
    return is_int_value(t) and t.as_long() == -1


def is_one(t: ExprRef) -> bool:
    return t.eq(ONE)


def is_zero(t: ExprRef) -> bool:
    return t.eq(ZERO)


def mk_true() -> BoolRef:
    return TRUE


def mk_false() -> BoolRef:
    return FALSE


def pairs2fla(pairs: list[tuple[ExprRef, ExprRef]]) -> BoolRef | bool:
    return mk_and(*[e == v for e, v in pairs])


def eval_sum(*args: ArithRef) -> IntNumRef:
    return z3.simplify(Sum(*args)) if args else ZERO


def eval_mul(*args: ArithRef) -> IntNumRef:
    return z3.simplify(Product(*args)) if args else ONE


def eval_exp(x: ArithRef, exp: int) -> ArithRef:
    assert exp >= 0
    return x if exp == 1 else eval_mul(exp * [x])


def mk_add(*args: ArithRef) -> ArithRef:
    if len(args) == 0:
        return ZERO
    if len(args) == 1:
        return args[0]
    return Sum(*args[1:]) if is_zero(args[0]) else Sum(*args)


def is_int_atom(a: ExprRef):
    return (
        ((z3.is_distinct(a) or z3.is_eq(a)) and a.num_args() >= 2 and is_int(a.arg(0)))
        or z3.is_le(a)
        or z3.is_lt(a)
        or z3.is_ge(a)
        or z3.is_gt(a)
    )


def mk_mul(*args: ArithRef) -> ArithRef:
    if len(args) == 1 and isinstance(args[0], list):
        return mk_mul(*args[0])
    if len(args) == 0:
        return ONE
    if len(args) == 1:
        return args[0]
    if any(is_zero(a) for a in args):
        return ZERO
    return Product(*args[1:]) if is_one(args[0]) else Product(*args)


def mk_pow(x: ExprRef, n: int) -> ArithRef:
    assert n >= 0
    return mk_mul(*(n * [x]))


def eval_pow(v: ExprRef, n: int) -> ArithRef:
    assert n >= 0
    return eval_mul(*(n * [v]))


def mk_or(*args: BoolRef) -> BoolRef:
    if len(args) == 1 and isinstance(args[0], list):
        return mk_or(*args[0])
    if len(args) == 1:
        return args[0]
    if len(args) == 0:
        return mk_false()
    if any(z3.is_true(a) for a in args):
        return mk_true()
    if len(args) == 2:
        a, b = args
        if is_false(a):
            return b
        if is_false(b):
            return a
    return z3.Or(*args)


def mk_and(*args: BoolRef) -> BoolRef:
    if len(args) == 1 and isinstance(args[0], list):
        return mk_and(*args[0])
    if len(args) == 0:
        return mk_true()
    if any(z3.is_false(a) for a in args):
        return mk_false()
    if len(args) == 1:
        return args[0]
    if len(args) == 2:
        a, b = args
        if is_true(a):
            return b
        if is_true(b):
            return a
    return z3.And(*args)


def mk_not(a: BoolRef | bool) -> BoolRef:
    if z3.is_not(a):
        return a.arg(0)
    if z3.is_true(a):
        return FALSE
    if z3.is_false(a):
        return TRUE
    return z3.Not(a)


def is_nnf_connective(a: ExprRef) -> bool:
    return is_and(a) or is_not(a) or is_or(a)


def is_non_linear(a: ExprRef) -> bool:
    return is_mul(a) and sum(not is_numeral(c) for c in a.children()) > 1


def is_numeral(a: ExprRef) -> bool:
    """Return True if a is a numeric literal (int, bool, algebraic, etc.)."""
    return (
        z3.is_true(a)
        or z3.is_false(a)
        or z3.is_int_value(a)
        or z3.is_algebraic_value(a)
        or z3.is_rational_value(a)
        or z3.is_bv_value(a)
    )


def is_symbolic_const(expr: ExprRef) -> bool:
    """Return True if expr is a non-numeric Z3 constant."""
    return z3.is_const(expr) and not is_numeral(expr)


def split_mul(t: ArithRef):
    assert is_mul(t)
    coeffs = []
    chs = t.children()
    pows = defaultdict(list)
    for ch in chs:
        if not is_numeral(ch):
            pows[ch].append(ch)
        else:
            coeffs.append(ch)
    k = eval_mul(*coeffs)
    assert 1 <= len(pows) <= 2, f"expected at most 2 vars in mono {pows}"
    return k, *pows.values()


_collect_symbolic_constants_memo: dict = {}


def collect_symbolic_constants(expr: ExprRef):
    """Return a set of symbolic Z3 constants in `expr`.

    Numerals and compound expressions are ignored.
    """
    if expr in _collect_symbolic_constants_memo:
        return _collect_symbolic_constants_memo[expr]
    if is_symbolic_const(expr):
        result = {expr}
    else:
        result = set()
        for child in expr.children():
            result |= collect_symbolic_constants(child)
    _collect_symbolic_constants_memo[expr] = result
    return result


def var_subs(e: ExprRef, subs: list[ExprRef], offset: int = 0, memo=None):
    """Give in an expression it substitutes all the variables by terms, which
    we assume are ground.

    This is useful when we're trying to remove a  quantifier. Then we
    run this function on the quantifier's body. The logic for vars is as
    follows, if a variable index is less than the offset, it means that
    we are replacing variables from the outer scope and nothing is
    changed. If the variable index is between the
    offset<vix<offset+len(subs), it is replaced by subs[vix-offset].
    otherwise, it means that it's from a scope outer to the quantifier
    that is being removed and therefore the variable's index is
    decreased by len(subs).
    """
    if memo is None:
        memo = {}
    if e in memo:
        return memo[e]
    result = None

    if z3.is_quantifier(e):
        # purposefully resetting the memo
        result = var_subs(e, subs, offset + e.num_vars(), None)  # type: ignore
    elif z3.is_var(e):
        vid = z3.get_var_index(e)
        if vid < offset:
            result = e
        elif vid < offset + len(subs):
            result = subs[vid - offset]
        else:
            result = z3.Var(vid - len(subs), e.sort())
    else:
        new_children = [var_subs(child, subs, offset, memo) for child in e.children()]
        if all(nc.eq(c) for nc, c in zip(new_children, e.children())):
            result = e
        else:
            result = e.decl()(*new_children)

    # Memoize the result
    memo[e] = result
    return result
