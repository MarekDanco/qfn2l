# Author:  mikolas
# Created on:  Tue Mar 17 2026
# Copyright (C) 2026, Mikolas Janota
#
"""Visitor classes for traversing and transforming z3 expressions."""

import typing
from abc import abstractmethod
from collections import defaultdict

from prefix import QLev
from utils import (
    ZERO,
    FALSE,
    TRUE,
    GetLevel,
    eval_mul,
    eval_sum,
    is_ite,
    is_numeral,
    is_symbolic_const,
    is_zero,
    is_one,
    is_true,
    is_false,
    mk_add,
    mk_and,
    mk_mul,
    mk_not,
    mk_or,
)
from z3 import (
    BoolRef,
    ExprRef,
    FreshConst,
    is_add,
    is_and,
    is_app,
    is_const,
    is_eq,
    is_mul,
    is_not,
    is_or,
    is_sub,
    simplify,
    substitute,
)


class SimpleVisit:
    """Naive memoized abstract class for visitor.

    Does not take into account quantifiers
    """

    def __init__(self):
        self._memo = {}

    @abstractmethod
    def visit_node(self, _e: ExprRef) -> typing.Any:
        """
        Abstract method: Subclasses MUST implement this method
        to visit nodes.
        """
        raise NotImplementedError("Not implemented")

    def __call__(self, a) -> typing.Any:
        return self.visit(a)

    def recurse(self, a: ExprRef) -> ExprRef:
        """Identity function for transformer visitors."""
        if not is_app(a) or a.num_args() == 0:
            return a
        rc = []
        change = False
        for ix in range(a.num_args()):
            c: ExprRef = a.arg(ix)
            nc = self(c)
            if not c.eq(nc):
                change = True
            rc.append(nc)
        if not change:
            return a
        return a.decl()(rc)

    def visit(self, a) -> typing.Any:
        """A memoized wrapper around visit_node."""
        if a in self._memo:
            return self._memo[a]
        res = self.visit_node(a)
        self._memo[a] = res
        return res


class Contains(SimpleVisit):
    def __init__(self, cs: set):
        super().__init__()
        self.cs = cs

    def visit_node(self, a):
        if a in self.cs:
            return True
        if not is_app(a):
            return False
        for i in range(a.num_args()):
            if self(a.arg(i)):
                return True
        return False


class HasUninterpreted(SimpleVisit):
    """Check for a given term whether it contains an uninterpreted constant."""

    def __init__(self):
        SimpleVisit.__init__(self)

    def __call__(self, a) -> bool:
        return super().__call__(a)

    def visit_node(self, e: ExprRef) -> bool:
        return (
            is_symbolic_const(e)
            or is_app(e)
            and any(self(e.arg(child_ix)) for child_ix in range(e.num_args()))
        )


class SimplePropagate(SimpleVisit):
    def __init__(self):
        super().__init__()

    def __call__(self, a) -> ExprRef:
        return super().__call__(a)

    def propagate(self, pos: bool, a: ExprRef):
        """Propagate simple equalities within an And (pos=True) or Or
        (pos=False).

        For And: extracts `x == c` where x is symbolic, substitutes into siblings.
        For Or: extracts `not(x == c)` (i.e. x != c), substitutes into siblings.

        Only handles equalities between constants (symbolic or literal).
        """

        def get_eq(e: ExprRef):
            """Extract equality (lhs, rhs) from a literal, with symbolic const
            on left.

            For pos=True: looks for `x == c` For pos=False: looks for
            `not(x == c)` Returns None if not a suitable equality.
            """
            if not pos:
                # Under Or, we need negated equalities: not(x == c)
                if is_not(e):
                    e = e.arg(0)
                else:
                    return None

            if not is_eq(e) or e.num_args() != 2:
                return None
            lhs, rhs = e.arg(0), e.arg(1)
            # Only handle equalities between constants (no complex terms)
            if not is_const(lhs) or not is_const(rhs):
                return None
            # Ensure symbolic constant is on the left
            if not is_symbolic_const(lhs):
                lhs, rhs = rhs, lhs
            if not is_symbolic_const(lhs):
                # Neither side is symbolic (e.g., 5 == 3)
                return None
            return lhs, rhs

        assert not pos or is_and(a)
        assert pos or is_or(a)

        chs: list[BoolRef] = a.children()
        eqs = set()
        i = 0
        # Extract equality literals from children (remove them from chs)
        while i < len(chs):
            if (eq := get_eq(chs[i])) is not None:
                # Swap-remove: replace current with last element
                chs[i] = chs[-1]
                chs.pop()
                eqs.add(eq)
            else:
                i += 1

        if not eqs:
            return a

        # Build substitution list, applying earlier substitutions to later rhs values
        # This handles chains like: x == y, y == 5 -> x maps to 5, y maps to 5
        substitution = []
        for lhs, rhs in eqs:
            rhs = substitute(rhs, *substitution)
            substitution.append((lhs, rhs))

        # Apply substitution to all remaining children
        chs = [substitute(ch, *substitution) for ch in chs]

        # Rebuild equality literals (skip trivial ones like x == x after substitution)
        eq_lits: list[BoolRef] = [
            lhs == rhs if pos else mk_not(lhs == rhs)
            for lhs, rhs in substitution
            if not lhs.eq(rhs)
        ]
        new_ch: list[BoolRef] = chs + eq_lits
        new_ch.sort(key=lambda c: c.get_id())
        return mk_and(*new_ch) if pos else mk_or(*new_ch)

    def visit_node(self, a):
        rv = self.recurse(a)
        if is_and(rv):
            return self.propagate(True, rv)
        if is_or(rv):
            return self.propagate(False, rv)
        return rv


class SimpleSimplify(SimpleVisit):
    def __init__(self):
        super().__init__()

    def visit_ite(self, a: ExprRef):
        cs = a.children()
        assert len(cs) == 3
        c, t, e = cs
        if is_true(c):
            return t
        if is_false(c):
            return e
        return a

    def visit_sub(self, a: ExprRef):
        return a.arg(0) if a.num_args() == 2 and is_zero(a.arg(1)) else a

    def visit_add(self, a: ExprRef):
        chs = a.children()
        if len(chs) == 1:
            return chs[0]

        coeffs = []
        others = []
        while chs:
            c = chs.pop()
            if is_numeral(c):
                coeffs.append(c)
            elif is_add(c):
                chs.extend(c.children())
            else:
                others.append(c)
        c = eval_sum(*coeffs)
        others.sort(key=lambda c: c.get_id())
        return mk_add(*others) if is_zero(c) else mk_add(c, *others)

    def visit_or(self, a: ExprRef):
        chs = a.children()
        nchs = []
        while chs:
            c = chs.pop()
            if is_true(c):
                return TRUE
            if is_or(c):
                chs.extend(c.children())
            elif not is_false(c):
                nchs.append(c)
        nchs.sort(key=lambda c: c.get_id())
        return mk_or(*nchs)

    def visit_and(self, a: ExprRef):
        chs = a.children()
        nchs = []
        while chs:
            c = chs.pop()
            if is_false(c):
                return FALSE
            if is_and(c):
                chs.extend(c.children())
            elif not is_true(c):
                nchs.append(c)
        nchs.sort(key=lambda c: c.get_id())
        return mk_and(*nchs)

    def visit_mul(self, a: ExprRef):
        chs = a.children()
        if len(chs) == 1:
            return chs[0]

        coeffs = []
        others = []
        while chs:
            c = chs.pop()
            if is_one(c):
                continue
            if is_zero(c):
                return ZERO
            if is_numeral(c):
                coeffs.append(c)
            elif is_mul(c):
                chs.extend(c.children())
            else:
                others.append(c)
        c = eval_mul(*coeffs)
        if is_zero(c):
            return ZERO
        others.sort(key=lambda c: c.get_id())
        if is_one(c):
            return mk_mul(*others)
        return mk_mul(c, *others)

    def visit_node(self, a):
        rv = self.recurse(a)
        if is_mul(rv):
            return self.visit_mul(rv)
        if is_add(rv):
            return self.visit_add(rv)
        if is_ite(rv):
            return self.visit_ite(rv)
        if is_sub(rv):
            return self.visit_sub(rv)
        if is_and(rv):
            return self.visit_and(rv)
        if is_or(rv):
            return self.visit_or(rv)
        if is_app(rv):
            n = rv.num_args()
            if n > 0 and all(is_numeral(rv.arg(i)) for i in range(n)):
                return simplify(rv)
        return rv


class MakeDefs(SimpleVisit):
    """Make definitions for complex expressions."""

    def __init__(self):
        SimpleVisit.__init__(self)
        self.hu = HasUninterpreted()
        self._definitions: dict[ExprRef, ExprRef] = {}

    def make(self, in_prefix, formula) -> tuple[list[QLev], ExprRef]:
        new_formula = self(formula)
        prefix = in_prefix[:]
        const2lev = {v: lev for lev, qlev in enumerate(prefix) for v in qlev.vars()}
        levs = GetLevel(const2lev)
        for t, v in self._definitions.items():
            new_level = -1
            term_level = levs(t)
            for lev in range(term_level, len(prefix)):
                qlev = prefix[lev]
                if qlev.is_exists():
                    qlev.add_var(v)
                    new_level = lev
                    break
            if new_level < 0:
                new_level = len(prefix)
                prefix.append(QLev(is_forall=False, vs=[v]))
            const2lev[v] = new_level
        return prefix, mk_and(
            new_formula, *[k == v for k, v in self._definitions.items()]
        )

    def __call__(self, a) -> ExprRef:
        return super().__call__(a)

    def _mk_def(self, t):
        d = self._definitions.get(t)
        if d is not None:
            return d
        nc = FreshConst(t.sort())
        self._definitions[t] = nc
        return nc

    def visit_node(self, init_t: ExprRef):
        t = self.recurse(init_t)
        if not t.eq(init_t):
            return self(t)
        if not is_mul(t):
            return t
        children = t.children()
        usymbols: int = sum(1 for c in children if self.hu(c))
        if usymbols < 2:
            return t
        coeffs = []
        splits = defaultdict(list)
        for c in children:
            if not self.hu(c):
                coeffs.append(c)
            else:
                x = c if is_const(c) else self._mk_def(c)
                splits[x].append(x)
        splits = list(splits.values())
        coeff = eval_mul(*coeffs)
        if is_zero(coeff):
            return ZERO
        while len(splits) > 2:
            a = splits.pop(0)
            b = splits.pop(0)
            d = self._mk_def(mk_mul(*a, *b))
            splits.append([d])
        assert 0 < len(splits) < 3
        return (
            mk_mul(coeff, *splits[0])
            if len(splits) == 1
            else mk_mul(coeff, *splits[0], *splits[1])
        )
