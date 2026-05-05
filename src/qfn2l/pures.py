# Author:  mikolas
# Created on:  Thu Jan 15 12:53:28 PM CET 2026
# Copyright (C) 2026, Mikolas Janota
#
from collections import defaultdict

from utils import (
    FALSE,
    TRUE,
    ZERO,
    is_false,
    is_int_atom,
    is_numeral,
    is_one,
    is_true,
    is_zero,
)
from visitors import HasUninterpreted, SimpleVisit
from z3 import (
    ArithRef,
    BoolRef,
    ExprRef,
    ModelRef,
    is_and,
    is_app,
    is_bool,
    is_const,
    is_eq,
    is_idiv,
    is_mod,
    is_mul,
    is_not,
    is_or,
    simplify,
)


class Pures:
    """A bijection between pures and terms."""

    def __init__(self) -> None:
        self._term2pure: dict[ArithRef, ArithRef] = dict()
        self._pure2term: dict[ArithRef, ArithRef] = dict()

    def map_t2p(self, t: ArithRef, p: ArithRef) -> None:
        assert t not in self._term2pure
        assert p not in self._pure2term
        self._term2pure[t] = p
        self._pure2term[p] = t

    def find_p(self, t: ArithRef) -> ArithRef | None:
        """Find pure corresponding to given term.

        return None if it doesn't exist
        """
        return self._term2pure.get(t, None)

    def get_t(self, p: ArithRef) -> ArithRef:
        """Get term corresponding to given pure."""
        rv = self.find_t(p)
        assert rv is not None, f"we expect that {p} is a pure"
        return rv

    def get_p(self, t: ArithRef) -> ArithRef:
        """Get pure corresponding to given term."""
        rv = self.find_p(t)
        assert rv is not None, f"we expect that {t} is a purified term"
        return rv

    def find_t(self, p: ArithRef) -> ArithRef | None:
        """Find term corresponding to given pure.

        return None if it doesn't exist
        """
        return self._pure2term.get(p, None)


class CollectPures(SimpleVisit):
    """Collect pure constants.

    The result is kept in the cumulatively collected set and not
    returned in the call.
    """

    def __init__(self, pures: Pures, axioms: defaultdict[ExprRef, list[BoolRef]]):
        SimpleVisit.__init__(self)
        self.pures = pures
        self.axioms = axioms
        self.collected: set[ArithRef] = set()
        # split by kind for zero-division interpretation and congruence axioms
        self.idiv_collected: set[ArithRef] = set()
        self.mod_collected: set[ArithRef] = set()
        self.mul_collected: set[ArithRef] = set()

    def __call__(self, a) -> None:
        return super().__call__(a)

    def visit_node(self, p: ExprRef):
        for i in range(p.num_args()):
            self(p.arg(i))
        t = self.pures.find_t(p)
        if t is not None:
            if p in self.collected:
                return
            self.collected.add(p)
            if is_idiv(t):
                self.idiv_collected.add(p)
            if is_mod(t):
                self.mod_collected.add(p)
            if is_mul(t):
                self.mul_collected.add(p)
            if p in self.axioms:
                for ax in self.axioms[p]:
                    self(ax)



class CheckVal(SimpleVisit):
    def __init__(self, hu: HasUninterpreted, pures: Pures, vals: ModelRef):
        SimpleVisit.__init__(self)
        self.hu = hu
        self.pures = pures
        self.vals = vals

    def check(self, f: ExprRef):
        rv = self(f)
        return rv is True or is_true(rv)

    def __call__(self, a) -> BoolRef | None:
        rv = super().__call__(a)
        return rv

    def visit_purified(self, original_term: ArithRef, p: ArithRef):
        tv = self(original_term)
        pv = self.vals.get_interp(p)
        return pv if tv is not None and pv == tv else None

    def visit_const(self, t: ExprRef):
        if is_and(t):
            return TRUE
        if is_or(t):
            return FALSE
        return t if is_numeral(t) else self.vals.get_interp(t)

    def visit_prop(self, t: ExprRef, cvs, cvset, has_none) -> BoolRef | None:
        def neg3(v: None | ExprRef):
            if v is None:
                return None
            assert isinstance(v, BoolRef), f"expecting {v} to be bool"
            if is_true(v):
                return FALSE
            if is_false(v):
                return TRUE
            raise ValueError(f"expected bool-like value, got {v}")

        if is_not(t):
            return neg3(cvs[0])
        if is_true(t):
            return TRUE
        if is_false(t):
            return FALSE
        if is_or(t):
            return TRUE if TRUE in cvset else (None if has_none else FALSE)
        if is_and(t):
            return FALSE if FALSE in cvset else (None if has_none else TRUE)
        if is_eq(t):
            a, b = cvs
            if a is None or b is None:
                return None
            cmp = a.eq(b)
            if isinstance(cmp, bool):
                return TRUE if cmp else FALSE
            raise TypeError(f"unexpected result from z3 eq: {cmp}")

    def visit_complex(self, t: ExprRef):
        cvs = [self(c) for c in t.children()]
        cvset = set(cvs)
        has_none = None in cvset
        if not has_none and all(not self.hu(c) for c in cvset):
            return simplify(t.decl()(*cvs))  # fully interpreted term

        if is_bool(t):
            return self.visit_prop(t, cvs, cvset, has_none)

        if is_mul(t) and ZERO in cvset:
            return ZERO
        if is_mod(t) and cvs[1] is not None and is_one(cvs[1]):
            return ZERO
        if has_none:  # bail
            return None
        if (is_mod(t) or is_idiv(t)) and is_zero(cvs[1]):
            return None
        return self.vals.eval(t.decl()(cvs))

    def visit_node(self, t: ExprRef):
        if isinstance(t, ArithRef) and (orig_t := self.pures.find_t(t)) is not None:
            return self.visit_purified(orig_t, t)
        return self.visit_const(t) if is_const(t) else self.visit_complex(t)


