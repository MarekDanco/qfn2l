#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Abstraction related functionality."""

# Created on:  Sun Dec 7 18:26:40 CET 2025
# Copyright (C) 2025, Mikolas Janota

import copy
import time
from collections import defaultdict
from itertools import combinations

import stats
import tagged_logging
import z3
from level_info import FormulaInfo
from prefix import QLev
from projections import (
    combine_lb,
    combine_ub,
    lin_lb_pow,
    lin_ub_pow,
    mod_ax_mul,
    project_y,
    triple_to_axiom,
)
from pures import CheckVal, CollectPures, Pures
from utils import (
    ONE,
    ZERO,
    eval_exp,
    eval_mul,
    eval_sum,
    is_neg,
    is_one,
    is_zero,
    mk_and,
    mk_mul,
    mk_not,
    mk_or,
    negate,
    pairs2fla,
)
from visitors import (
    HasUninterpreted,
    SimplePropagate,
    SimpleSimplify,
    SimpleVisit,
)
from z3 import (
    ArithRef,
    BoolRef,
    ExprRef,
    FreshConst,
    Implies,
    IntVal,
    ModelRef,
    Or,
    SolverFor,
    is_idiv,
    is_mod,
    is_mul,
    simplify,
    substitute,
)

LOG_TAG = "abs"
module_log = tagged_logging.mk_logfn(LOG_TAG)


class LIAFail(Exception):
    pass


class LiaAbstraction:
    """Represents the lia abstraction for a formula.

    Keep track of which variables are abstract and to which term they
    correspond.
    """

    def log(self, log_lev: int, *args, **kwargs):
        """Logging for this class, which also outputs the level."""
        module_log(
            log_lev,
            f"<{'e' if self.is_exists else 'u'}@{self.current_level}>",
            *args,
            **kwargs,
        )

    class Purifier(SimpleVisit):
        """Purify the given formula."""

        def __init__(self, parent: "LiaAbstraction"):
            SimpleVisit.__init__(self)
            self.parent = parent
            self.hu = parent.hu

        def __call__(self, a) -> ExprRef:
            return super().__call__(a)

        def _visit_idiv(self, t: ExprRef):
            assert is_idiv(t), f"Expected div got {t}"
            x, y = t.arg(0), t.arg(1)
            if self.hu(y) or is_zero(y):
                p = self.parent.make_pure_constant(t)
                if self.parent.opts.static and not is_zero(y):
                    self.parent.add_axiom(p, Implies(y != 0, z3.Abs(p) <= z3.Abs(x)))
                return p
            return t

        def _visit_mod(self, t: ExprRef):
            assert is_mod(t), f"Expected mod got {t}"
            y = t.arg(1)
            if self.hu(y) or is_zero(y):
                p = self.parent.make_pure_constant(t)
                if self.parent.opts.static and not is_zero(y):
                    self.parent.add_axiom(
                        p, Implies(y != 0, mk_and(0 <= p, p < z3.Abs(y)))
                    )
                return p
            return t

        def _visit_mul(self, t: ExprRef):
            assert is_mul(t), f"Expected multiplication got {t}"
            cs = t.children()
            css = sorted(cs, key=lambda c: c.get_id())
            if cs != css:
                return self(t.decl()(css))
            coeffs = []
            others = []
            for c in cs:
                if self.hu(c):
                    others.append(c)
                else:
                    coeffs.append(simplify(c))
            if len(others) <= 1:
                return t
            c = eval_mul(*coeffs)
            o = mk_mul(*others)
            p = self.parent.make_pure_constant(o)
            return mk_mul(c, p)

        def visit_node(self, a):
            t = self.recurse(a)  # purify children
            if not a.eq(t):
                return self(t)
            if is_idiv(t):
                return self._visit_idiv(t)
            if is_mod(t):
                return self._visit_mod(t)
            if is_mul(t):
                return self._visit_mul(t)
            return t

    def __init__(self, opts, level_info: FormulaInfo, is_exists: bool):
        self.opts = opts
        self.level_info = FormulaInfo(prefix=level_info.prefix, body=level_info.body)
        self.current_level = -1
        self.assignment = None
        self.axioms: defaultdict[ExprRef, list[BoolRef]] = defaultdict(list)
        self.pures = Pures()
        self.hu = HasUninterpreted()
        self.current_pure_body = None
        # run purification on the body
        self.purify = self.Purifier(self)
        self.simpl = SimpleSimplify()
        self.prop = SimplePropagate()
        self.is_exists = is_exists

        self.prefix = copy.deepcopy(self.level_info.prefix)
        self.body = self.level_info.body
        self.purify(self.body)
        self.current_model: ModelRef | None = None
        self.current_solver: z3.Solver | None = None
        self.current_instantiation: BoolRef | None = None
        self.current_pure_body: BoolRef | None = None
        self.log(3, self.prefix, self.body)
        self.log(4, self.pures._term2pure)

        self.mod_zero_interp = {}
        self.idiv_zero_interp = {}

    def _congruence_same_axioms(self, pures_set: set) -> list:
        """Congruence axioms for div/mod pures: same args => same pure."""
        axioms = []
        for a, b in combinations(pures_set, 2):
            a_term = self.pures.get_t(a)
            b_term = self.pures.get_t(b)
            ax, ay = a_term.children()
            bx, by = b_term.children()
            ax = self.pures.find_p(ax) or ax
            ay = self.pures.find_p(ay) or ay
            bx = self.pures.find_p(bx) or bx
            by = self.pures.find_p(by) or by
            axioms.append(Implies(mk_and(ax == bx, ay == by), a == b))
        return axioms

    def _congruence_commutative_axioms(self, pures_set: set) -> list:
        """Congruence axioms for mul pures: equal-exponent args in any order
        => same pure."""

        def get_pws(t):
            pw1, pw2 = t
            return (pw1[0], len(pw1)), (pw2[0], len(pw2))

        axioms = []
        for a, b in combinations(pures_set, 2):
            spla = self.split_mul(self.pures.get_t(a))
            splb = self.split_mul(self.pures.get_t(b))
            assert 2 <= len(spla) <= 3
            assert 2 <= len(splb) <= 3
            if (
                len(spla) != 3
                or len(splb) != 3
                or not is_one(spla[0])
                or not is_one(splb[0])
            ):
                continue
            (ar1, ae1), (ar2, ae2) = get_pws(spla[1:])
            (br1, be1), (br2, be2) = get_pws(splb[1:])
            if ae1 == be1 and ae2 == be2:
                axioms.append(Implies(mk_and(ar1 == br1, ar2 == br2), a == b))
            if ae1 == be2 and ae2 == be1:
                axioms.append(Implies(mk_and(ar1 == br2, ar2 == br1), a == b))
        return axioms

    def _congruence_pow_order_axioms(self, pures_set: set) -> list:
        """Monotonicity axioms for pairs of single-variable same-exponent power pures.
        Odd k:  ra <= rb <-> pa <= pb  (t^k strictly monotone on Z)
        Even k: four sign-quadrant cases encoding |ra| <= |rb| <-> pa <= pb
        """
        pow_pures = []
        for p in pures_set:
            spl = self.split_mul(self.pures.get_t(p))
            if len(spl) != 2 or not is_one(spl[0]):
                continue
            pow_pures.append((p, spl[1][0], len(spl[1])))

        axioms = []
        for (pa, ra, ea), (pb, rb, eb) in combinations(pow_pures, 2):
            if ea != eb:
                continue
            if ea % 2 == 1:
                # both orderings needed: combinations gives one; the other catches
                # cases where pures are equal but roots differ in the other direction
                axioms.append((ra <= rb) == (pa <= pb))
                axioms.append((rb <= ra) == (pb <= pa))
            else:
                nra, nrb = -ra, -rb
                axioms.append(
                    Implies(mk_and(ra >= ZERO, rb >= ZERO), (ra <= rb) == (pa <= pb))
                )
                axioms.append(
                    Implies(mk_and(ra >= ZERO, rb >= ZERO), (rb <= ra) == (pb <= pa))
                )
                axioms.append(
                    Implies(mk_and(ra <= ZERO, rb <= ZERO), (rb <= ra) == (pa <= pb))
                )
                axioms.append(
                    Implies(mk_and(ra <= ZERO, rb <= ZERO), (ra <= rb) == (pb <= pa))
                )
                axioms.append(
                    Implies(mk_and(ra >= ZERO, rb <= ZERO), (ra <= nrb) == (pa <= pb))
                )
                axioms.append(
                    Implies(mk_and(ra >= ZERO, rb <= ZERO), (nrb <= ra) == (pb <= pa))
                )
                axioms.append(
                    Implies(mk_and(ra <= ZERO, rb >= ZERO), (nra <= rb) == (pa <= pb))
                )
                axioms.append(
                    Implies(mk_and(ra <= ZERO, rb >= ZERO), (rb <= nra) == (pb <= pa))
                )
        return axioms

    def mk_congruence_axioms(self, _pures: CollectPures):
        """Create congruence axioms of the collected pures."""
        return (
            self._congruence_same_axioms(_pures.idiv_collected)
            + self._congruence_same_axioms(_pures.mod_collected)
            + self._congruence_commutative_axioms(_pures.mul_collected)
            + self._congruence_pow_order_axioms(_pures.mul_collected)
        )

    def get_level(self, t: ExprRef) -> int:
        return self.level_info.get_level(t)

    def make_pure_constant(self, term: ArithRef) -> ArithRef:
        """Creates a new constant for the given term and adds it to the prefix
        at the proper place."""

        def make_fancy_name(term) -> str:
            plname = ("e" if self.is_exists else "u") + "_"
            if not is_mul(term):
                return plname
            children = term.children()
            children_set = set(children)
            if len(children_set) == 1:
                return f"{plname}{children[0].decl()}^{len(children)}"
            nms = "".join(str(c.decl()) for c in children)
            return f"{plname}{nms}"

        pure = self.pures.find_p(term)
        if pure is not None:
            return pure
        term_level = self.get_level(term)
        pure = FreshConst(term.sort(), make_fancy_name(term))
        assert isinstance(pure, ArithRef)
        self.pures.map_t2p(term, pure)
        stats.STATS.pures += 1
        self.log(4, f"mapping {term} to {pure}")
        new_level = -1
        for lev in range(term_level, len(self.prefix)):
            qlev = self.prefix[lev]
            if qlev.is_exists():
                qlev.add_var(pure)
                new_level = lev
                break
        if new_level < 0:
            new_level = len(self.prefix)
            self.prefix.append(QLev(is_forall=False, vs=[pure]))
        self.level_info.add_const(pure, new_level)

        if is_mul(term):
            chs = set(term.children())
            self.add_axiom(
                pure, mk_or(*[c == ZERO for c in chs]) == (pure == ZERO), "smul"
            )
            t = self.split_mul(term)
            coeff, pws = t[0], t[1:]
            assert is_one(coeff)
            oddroots = [pow[0] for pow in pws if len(pow) % 2 != 0]
            evenroots = [pow[0] for pow in pws if len(pow) % 2 == 0]
            if len(oddroots) == 2:
                assert not evenroots
                self.add_axiom(
                    pure,
                    mk_or(
                        mk_and(oddroots[1] > ZERO, oddroots[0] > ZERO),
                        mk_and(oddroots[0] < ZERO, oddroots[1] < ZERO),
                    )
                    == (pure > ZERO),
                    "smul",
                )
            elif len(oddroots) == 1:
                self.add_axiom(
                    pure,
                    mk_and(*[r != ZERO for r in evenroots], oddroots[0] > ZERO)
                    == (pure > ZERO),
                    "smul",
                )
            elif len(oddroots) == 0:
                self.add_axiom(pure, pure >= ZERO, "smul")
            else:
                raise AssertionError(f"unexpected oddroots state: {oddroots}")

        return pure

    def add_axiom(self, pure: ExprRef, ax: BoolRef, tag=""):
        self.log(4, f"ax: {ax} {tag}")
        self.axioms[pure].append(ax)

    def add_axioms(self, pure: ExprRef, axs, tag=""):
        for ax in axs:
            self.add_axiom(pure, ax, tag)

    def set_level(self, level: int, assignment: dict[ExprRef, ExprRef]):
        """Instantiate the current abstraction under the given assignment."""
        stats.STATS.begin_phase(stats.STATS.set_level_time)
        assert isinstance(assignment, dict)
        self.assignment = assignment
        self.current_level = level
        subs = list(self.assignment.items())
        self.current_body = self.prop(self.simpl(substitute(self.body, subs)))
        self.current_pure_body = self.purify(self.current_body)
        pcol = CollectPures(self.pures, self.axioms)
        pcol(self.current_pure_body)
        substituted_congruence = self.simpl(
            substitute(mk_and(*self.mk_congruence_axioms(pcol)), subs)
        )
        self.current_pure_body = mk_and(self.current_pure_body, substituted_congruence)
        assert self.current_pure_body is not None
        self.current_instantiation = mk_and(
            self.current_pure_body,
            *[
                self.simpl(substitute(ax, subs))
                for ls in self.axioms.values()
                for ax in ls
            ],
        )
        self.log(3, "inst by:", assignment, self.current_instantiation)
        stats.STATS.end_phase()

    def solve(self) -> ModelRef | None:
        """Solve the current LIA abstraction.

        Returns a model upon success, otherwise returns None.
        Additionally ensures that all variables in the current level
        have value.
        """

        def complete_model(model: ModelRef) -> None:
            for c in self.level_info.prefix[self.current_level].vars():
                if model[c] is not None:
                    continue
                default_value = model.eval(c, model_completion=True)
                self.log(5, f"solve: creating a default value for {c}")
                model.update_value(c, default_value)

        stats.STATS.begin_phase(stats.STATS.solve_time)
        self._solve()
        if self.current_model is None:
            stats.STATS.end_phase()
            return None
        stats.STATS.begin_phase(stats.STATS.complete_model_time)
        complete_model(self.current_model)
        stats.STATS.end_phase()
        stats.STATS.end_phase()
        return self.current_model

    def incorporate_assumptions(self, assumptions, msg) -> ModelRef | None:
        assert self.current_solver is not None
        while assumptions:
            self.log(3, f"incorporating {msg} assumptions", assumptions)
            self.current_solver.set("timeout", self.opts.heur_to)
            res = stats.timed_check(self.current_solver, *assumptions)
            self.current_solver.set("timeout", 0)
            if res == z3.unknown:
                self.log(
                    2,
                    f"incorporating {msg} assumptions yielded unknown",
                )
                return None
            elif res == z3.unsat:
                confl = self.current_solver.unsat_core()
                for p in confl:
                    self.log(3, f"unsuccessfull {msg} assumption", p)
                    assumptions.remove(p)
            elif res == z3.sat:
                self.log(2, f"successfull {msg} assumptions", assumptions)
                self.log(4, f"{msg} model", self.current_solver.model())
                return self.current_solver.model()
            else:
                self.log(1, f"{msg} unexpected result")
                raise ValueError(f"unexpected result from z3: {res}")
        return None

    def _solve(self) -> None:
        """Solve the current LIA abstraction.

        Updates self.current_model with model upon success, otherwise
        sets it to None.
        """
        self.current_solver = SolverFor("LIA")
        self.current_solver.add(self.current_instantiation)
        self.current_model = None
        self.log(4, "SAT?:", self.current_solver.assertions())

        if self.opts.timeout > 0:
            remaining_ms = int(
                (self.opts.timeout - (time.time() - self.opts.start_time)) * 1000
            )
            self.current_solver.set("timeout", max(1, remaining_ms))

        res = stats.timed_check(self.current_solver)
        self.log(4, "check done")
        if res == z3.sat:
            self.current_model = self.current_solver.model()
        elif res == z3.unknown:
            self.log(-1, "LIA solver fail")
            self.log(4, res)
            raise LIAFail("we didn't budget for lia not being solved")
        else:
            assert self.current_model is None
            return
        self.log(4, "raw model", self.current_model)

        if not (self.opts.bounds or self.opts.zeros):
            return
        pcol = CollectPures(pures=self.pures, axioms=self.axioms)
        pcol(self.current_pure_body)
        cur_pures = {
            p for p in pcol.collected if self.get_level(p) <= self.current_level
        }
        if not cur_pures:
            return

        assumptions: set[BoolRef] = set()
        if self.opts.zeros:
            assumptions = self._apply_zeros_heuristic(cur_pures)
        if self.opts.bounds:
            self._apply_bounds_heuristic(cur_pures, assumptions)

    def _apply_zeros_heuristic(self, cur_pures) -> set[BoolRef]:
        """Try to set multiplication pures to zero. Returns the assumption set used."""
        assumptions = {p == ZERO for p in cur_pures if is_mul(self.pures.get_t(p))}
        m = self.incorporate_assumptions(assumptions, "zeros")
        if m is not None:
            self.current_model = m
        return assumptions

    def _apply_bounds_heuristic(self, cur_pures, assumptions) -> None:
        """Iteratively try to shrink pure values by adding bound constraints."""
        assert self.current_solver is not None
        for attempt in range(5):
            assert self.current_model is not None
            pure_vals = {
                self.current_model.eval(p, model_completion=True).as_long()
                for p in cur_pures
            }
            mx = max(abs(v) for v in pure_vals)
            if mx < 20:
                return
            lb = IntVal(-3 * mx // 4)
            ub = IntVal(3 * mx // 4)
            self.log(4, "lowering attempt:", attempt)
            self.log(3, f"trying with bounds {lb}-{ub} from {mx}")
            bounds = [lb < p for p in cur_pures] + [p < ub for p in cur_pures]
            self.log(5, "bounds:", bounds)
            self.current_solver.set("timeout", self.opts.heur_to)
            res = stats.timed_check(self.current_solver, *assumptions, *bounds)
            self.current_solver.set("timeout", 0)
            if res != z3.sat:
                return
            self.current_model = self.current_solver.model()
            self.log(4, "raw bounded model", self.current_model)

    def split_mul(self, t: ArithRef):
        assert is_mul(t)
        coeffs = []
        chs = t.children()
        pows = defaultdict(list)
        for ch in chs:
            if self.hu(ch):
                pows[ch].append(ch)
            else:
                coeffs.append(ch)
        k = eval_mul(*coeffs)
        assert 1 <= len(pows) <= 2
        return k, *pows.values()

    def mk_pow_axioms(self, assignment: ModelRef, pure, split):
        assert len(split) == 1
        pow = split[0]
        root = pow[0]
        root_val = assignment.get_interp(root)
        if root_val is None:  # nothing assigned
            return []
        exp = len(pow)
        rv = []
        # equality axioms
        if is_zero(root_val):
            rv.append((pure == ZERO) == (root == ZERO))
        else:
            odd = exp % 2 == 1
            premise = root == root_val
            tval = eval_exp(root_val, exp)
            if odd:
                rv.append((pure == tval) == premise)
                # x^k <= v^k OR x^k >= (v+1)^k
                root_val1 = eval_sum(root_val, ONE)
                tval1 = eval_exp(root_val1, exp)
                rv.append(Or(pure <= tval, pure >= tval1))
            else:
                premise1 = root == negate(root_val)
                rv.append((pure == tval) == Or(premise, premise1))
                # abs value for root

                # x^k <= |v|^k OR x^k >= |v+1|^k
                ar = negate(root_val) if is_neg(root_val) else root_val
                ar1 = eval_sum(ar, ONE)
                tval1 = eval_exp(ar1, exp)
                rv.append(Or(pure <= tval, pure >= tval1))

        # inequality axioms
        clb, projlb = lin_lb_pow(root, exp, root_val)
        cub, projub = lin_ub_pow(root, exp, root_val)
        rv += [
            triple_to_axiom(clb, projlb, pure),
            triple_to_axiom(cub, pure, projub),
        ]
        # mod axioms
        if (
            self.opts.modax > 2
            and (pure_val := assignment.get_interp(pure)) is not None
        ):
            rv += mod_ax_mul(self.opts.modax, [(root, exp, root_val)], pure, pure_val)
        return rv

    def mk_mixed_mul_axioms(self, t: ArithRef, assignment: ModelRef, pure, split):
        assert len(split) == 2
        # all pures must contain at most two powers
        pow1, pow2 = split
        if assignment.get_interp(pow1[0]) is None:
            pow1, pow2 = pow2, pow1
        root1 = pow1[0]
        root2 = pow2[0]
        root1_val = assignment.get_interp(root1)
        root2_val = assignment.get_interp(root2)
        if root1_val is None:  # nothing assigned
            return []
        exp1 = len(pow1)
        exp2 = len(pow2)
        premise = [
            (r, rv)
            for r in (root1, root2)
            if (rv := assignment.get_interp(r)) is not None
        ]
        tsubs = self.simpl(substitute(t, premise))
        eq_axiom = Implies(pairs2fla(premise), pure == tsubs)
        rv = []
        rv.append(eq_axiom)
        if root2_val is None:
            ppow2 = self.purify(mk_mul(*pow2))
            assert isinstance(ppow2, ArithRef)
            rv += [
                triple_to_axiom(*t)
                for t in project_y(
                    x=root2,
                    x_exp=exp2,
                    y=root1,
                    y_exp=exp1,
                    y_val=root1_val,
                    pure_xm=ppow2,
                    pure_res=pure,
                )
            ]
            return rv
        assert root1_val is not None and root2_val is not None
        for c, b in combine_lb(root1, exp1, root1_val, root2, exp2, root2_val):
            rv.append(triple_to_axiom(c, b, pure))
        for c, b in combine_ub(root1, exp1, root1_val, root2, exp2, root2_val):
            rv.append(triple_to_axiom(c, pure, b))
        if (
            self.opts.modax > 1
            and (pure_val := assignment.get_interp(pure)) is not None
        ):
            rv += mod_ax_mul(
                self.opts.modax,
                [(root1, exp1, root1_val), (root2, exp2, root2_val)],
                pure,
                pure_val,
            )
        return rv

    def mk_mul_axioms(self, assignment: ModelRef, t: ArithRef):
        spl = self.split_mul(t)
        assert is_one(spl[0]), f"pures for mul shouldn't contain coefficients {t}"
        spl = spl[1:]
        assert 1 <= len(spl) <= 2, f"we expect only two variables in a monomial {t}"
        pure = self.pures.get_p(t)
        return (
            self.mk_pow_axioms(assignment, pure, spl)
            if len(spl) == 1
            else self.mk_mixed_mul_axioms(t, assignment, pure, spl)
        )

    def mk_mod_axiom(self, assignment: ModelRef, t: ArithRef):
        x, y = t.arg(0), t.arg(1)
        xval = assignment.eval(x)
        yval = assignment.eval(y)
        tsubs_x = self.simpl(substitute(x, (y, yval)))
        tsubs_y = self.simpl(substitute(y, (x, xval)))
        pure = self.pures.get_p(t)
        axioms = []
        if not self.hu(xval):
            xv = xval.as_long()
            if xv >= 0:
                axioms.append(
                    Implies(mk_and(x == xval, z3.Abs(tsubs_y) > xv), pure == xval)
                )
            else:
                axioms.append(
                    Implies(
                        mk_and(x == xval, z3.Abs(tsubs_y) > abs(xv)),
                        pure == xval + z3.Abs(tsubs_y),
                    )
                )
        if not self.hu(yval) and not is_zero(yval):
            axioms.append(Implies(y == yval, pure == tsubs_x % yval))
        return axioms

    def mk_idiv_axiom(self, assignment: ModelRef, t: ArithRef):
        x, y = t.arg(0), t.arg(1)
        xval = assignment.eval(x)
        yval = assignment.eval(y)
        tsubs_x = self.simpl(substitute(x, (y, yval)))
        tsubs_y = self.simpl(substitute(y, (x, xval)))
        pure = self.pures.get_p(t)
        axioms = []
        if not self.hu(xval):
            xv = xval.as_long()
            if xv >= 0:
                axioms.append(
                    Implies(
                        mk_and(x == xval, z3.Abs(tsubs_y) > xv), pure == z3.IntVal(0)
                    )
                )
            else:
                axioms.append(
                    Implies(
                        mk_and(x == xval, z3.Abs(tsubs_y) >= abs(xv)),
                        pure == z3.If(tsubs_y > 0, -1, 1),
                    )
                )
        if not self.hu(yval) and not is_zero(yval):
            axioms.append(Implies(y == yval, pure == tsubs_x / yval))
        return axioms

    def is_okay(self, pure, t, current_model):
        """Check if value of pure is equal to the real value of the
        corresponding NIA term t.

        If t is zero division a new interpretation is created.
        """
        self.log(4, f"check_nia: {pure} == {t}")
        pure_val = current_model.eval(pure)
        if (is_mod(t) or is_idiv(t)) and is_zero(current_model.eval(t.arg(1))):
            self.log(2, f"check_nia: zero division in {t}")
            t1_val = current_model.eval(t.arg(0))
            if is_mod(t):
                self.log(3, f"setting {t1_val}%0 = {pure_val}")
                self.mod_zero_interp[t1_val] = pure_val
            else:
                self.log(3, f"setting {t1_val}/0 = {pure_val}")
                self.idiv_zero_interp[t1_val] = pure_val
            return True
        tval = current_model.eval(t)
        self.log(4, f"check_nia: --> {pure_val} == {tval}")
        return tval.eq(pure_val)

    def check_nia(self, assignment: ModelRef) -> bool:
        """Check if the nia operations are ok under the current assignment and
        adds axioms.

        Adds axioms for non-lia terms at the current level. Returns true
        iff no violations were found, i.e. no axioms were added.
        """
        assert self.current_model is not None, "should have been after sat result"
        assert self.current_solver is not None
        assert self.current_pure_body is not None
        self.log(3, f"check_nia assignment: {assignment}")
        stats.STATS.begin_phase(stats.STATS.check_nia_time)

        three_valued_checker = CheckVal(self.hu, self.pures, self.current_model)
        if three_valued_checker.check(self.current_pure_body):
            self.log(2, "check_nia quick ok")
            stats.STATS.end_phase()
            return True

        res = True
        pcol = CollectPures(pures=self.pures, axioms=self.axioms)
        pcol(self.current_pure_body)

        for pure in pcol.collected:
            t = self.pures.get_t(pure)
            if self.hu(t) and self.get_level(t) != self.current_level:
                continue
            is_okay = self.is_okay(pure, t, self.current_model)
            self.log(4, f"check_nia result: {is_okay}")
            if is_okay:
                continue
            res = False
            self.log(3, f"check_nia: axioms for {t} .. {pure}")
            if z3.is_idiv(t):
                axioms = self.mk_idiv_axiom(assignment, t)
                self.add_axioms(pure, axioms, "div")
                stats.STATS.div_axioms += len(axioms)
                continue
            if z3.is_mod(t):
                axioms = self.mk_mod_axiom(assignment, t)
                self.add_axioms(pure, axioms, "mod")
                stats.STATS.mod_axioms += len(axioms)
                continue
            if z3.is_mul(t):
                axioms = self.mk_mul_axioms(self.current_model, t)
                self.add_axioms(pure, axioms, "mul")
                stats.STATS.mul_axioms += len(axioms)
                continue
        stats.STATS.end_phase()
        return res
