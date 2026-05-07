"""Bound projections for nonlinear terms used in axiom generation.

Functions come in two flavours:
  - const_*: the bound is a constant  x_val^n  (tight at the model point)
  - lin_*:   the bound is linearized  x_val^(n-1) * x  (linear in x, valid in
             the region around x_val, tighter for values close to x_val)

Both flavours return a (condition, bound) pair.  The condition restricts x to
the region where the bound is valid; the bound is used to build an axiom of
the form  condition => x^n >= bound  (or <=).
"""

from itertools import product

from utils import (
    ZERO,
    eval_pow,
    is_neg,
    mk_and,
    mk_mul,
    mk_pow,
)
from visitors import SimpleSimplify
from z3 import (
    ArithRef,
    BoolRef,
    Implies,
    Int,
    IntNumRef,
    IntVal,
    is_true,
    simplify,
    substitute,
)


def const_lb_pow(x: ArithRef, n: int, x_val: ArithRef) -> tuple[BoolRef, ArithRef]:
    """Return (condition, bound) s.t. condition => x^n >= bound, with bound = x_val^n.

    The condition constrains x relative to x_val, accounting for sign flips
    when n is odd (x^n is monotone) vs even (x^n is symmetric around 0).
    """
    if not is_neg(x_val):
        return x >= x_val, eval_pow(x_val, n)
    if n % 2 == 0:
        return x <= x_val, eval_pow(x_val, n)
    return mk_and(x_val <= x, x <= ZERO), eval_pow(x_val, n)


def const_ub_pow(x: ArithRef, n: int, x_val: ArithRef) -> tuple[BoolRef, ArithRef]:
    """Return (condition, bound) s.t. condition => x^n <= bound, with bound = x_val^n.

    The condition constrains x relative to x_val, accounting for sign flips
    when n is odd (x^n is monotone) vs even (x^n is symmetric around 0).
    """
    if not is_neg(x_val):
        return mk_and(ZERO <= x, x <= x_val), eval_pow(x_val, n)
    if n % 2 == 0:
        return mk_and(x_val <= x, x <= ZERO), eval_pow(x_val, n)
    return x <= x_val, eval_pow(x_val, n)


def lin_lb_pow(x: ArithRef, n: int, x_val: ArithRef) -> tuple[BoolRef, ArithRef]:
    """Return (condition, linear_bound) s.t. condition => x^n >= linear_bound.

    Like const_lb_pow but the bound is linearized: x_val^(n-1) * x instead of
    x_val^n. This is a valid lower bound on x^n in the region where x >= x_val
    (or the sign-adjusted equivalent), and is tighter for x close to x_val.
    """
    if not is_neg(x_val):
        return x >= x_val, mk_mul(eval_pow(x_val, n - 1), x)
    if n % 2 == 0:
        return x <= x_val, mk_mul(eval_pow(x_val, n - 1), x)
    return mk_and(x_val <= x, x <= ZERO), mk_mul(eval_pow(x_val, n - 1), x)


def lin_ub_pow(x: ArithRef, n: int, x_val: ArithRef) -> tuple[BoolRef, ArithRef]:
    """Return (condition, linear_bound) s.t. condition => x^n <= linear_bound.

    Like const_ub_pow but the bound is linearized: x_val^(n-1) * x instead of
    x_val^n.
    """
    if not is_neg(x_val):
        return mk_and(ZERO <= x, x <= x_val), mk_mul(eval_pow(x_val, n - 1), x)
    if n % 2 == 0:
        return mk_and(x_val <= x, x <= ZERO), mk_mul(eval_pow(x_val, n - 1), x)
    return x <= x_val, mk_mul(eval_pow(x_val, n - 1), x)


def is_negative_power(root: IntNumRef, exp: int) -> bool:
    """Return true iff root^exp is negative (i.e. exp is odd and root < 0)."""
    return exp % 2 == 1 and is_neg(root)


def combine_lb_left(
    x: ArithRef, x_exp: int, x_val: IntNumRef, y: ArithRef, y_exp: int, y_val: IntNumRef
) -> list[tuple[BoolRef, ArithRef]]:
    """Return lower bound axioms for x^x_exp * y^y_exp with x linearized.

    Returns a list of (condition, lower_bound) pairs such that
    condition => x^x_exp * y^y_exp >= lower_bound,
    where x appears linearly in lower_bound (via lin_lb_pow/lin_ub_pow)
    and y appears as a constant bound (via const_lb_pow/const_ub_pow).

    When y^y_exp is negative it flips x's contribution (lin_lb ↔ lin_ub);
    when x^x_exp is negative it flips y's contribution (const_lb ↔ const_ub).
    """
    neg_x = is_negative_power(x_val, x_exp)
    neg_y = is_negative_power(y_val, y_exp)
    x_cond, x_bound = (lin_lb_pow if not neg_y else lin_ub_pow)(x, x_exp, x_val)
    y_cond, y_bound = (const_lb_pow if not neg_x else const_ub_pow)(y, y_exp, y_val)
    return [(mk_and(x_cond, y_cond), mk_mul(x_bound, y_bound))]  # SimpleSimplify()


def combine_lb(
    x: ArithRef, x_exp: int, x_val: IntNumRef, y: ArithRef, y_exp: int, y_val: IntNumRef
):
    """Return lower bound axioms for x^x_exp * y^y_exp, linearizing each variable
    in turn."""
    return combine_lb_left(x, x_exp, x_val, y, y_exp, y_val) + combine_lb_left(
        y, y_exp, y_val, x, x_exp, x_val
    )


def combine_ub_left(
    x: ArithRef, x_exp: int, x_val: IntNumRef, y: ArithRef, y_exp: int, y_val: IntNumRef
) -> list[tuple[BoolRef, ArithRef]]:
    """Return upper bound axioms for x^x_exp * y^y_exp with x linearized.

    Mirror of combine_lb_left with lb/ub swapped: returns (condition, upper_bound)
    pairs such that condition => x^x_exp * y^y_exp <= upper_bound.
    """
    neg_x = is_negative_power(x_val, x_exp)
    neg_y = is_negative_power(y_val, y_exp)
    x_cond, x_bound = (lin_ub_pow if not neg_y else lin_lb_pow)(x, x_exp, x_val)
    y_cond, y_bound = (const_ub_pow if not neg_x else const_lb_pow)(y, y_exp, y_val)
    return [(mk_and(x_cond, y_cond), mk_mul(x_bound, y_bound))]  # SimpleSimplify()


def combine_ub(
    x: ArithRef, x_exp: int, x_val: IntNumRef, y: ArithRef, y_exp: int, y_val: IntNumRef
):
    """Return upper bound axioms for x^x_exp * y^y_exp, linearizing each variable
    in turn."""
    return combine_ub_left(x, x_exp, x_val, y, y_exp, y_val) + combine_ub_left(
        y, y_exp, y_val, x, x_exp, x_val
    )


def triple_to_axiom(condition: BoolRef, lhs: ArithRef, rhs: ArithRef):
    """Convert a (condition, lhs, rhs) triple to the axiom: condition => lhs <= rhs."""
    return Implies(condition, lhs <= rhs)


def mod_ax_mul(
    max_modulus: int,
    factors: list[tuple[ArithRef, int, IntNumRef]],
    pure: ArithRef,
    pure_val: IntNumRef,
):
    """Generate a modular congruence axiom for a product of powers.

    For each modulus k in [2, max_modulus], checks whether the current model
    value of `pure` has the correct residue mod k given the factor values.
    If not, returns an axiom of the form:
    Implies(root1 % k == r1 and root2 % k == r2 and ..., pure % k == expected)

    `factors` is a list of (variable, exponent, current_value) triples
    representing the factors of the product abstracted by `pure`,
    e.g. [(x, 2, 3), (y, 1, -1)] for the term x²·y with x=3, y=-1.
    """
    factors_int = [(root, exp, v.as_long()) for (root, exp, v) in factors]
    pure_int = pure_val.as_long()
    for k in range(2, max_modulus + 1):
        expected_residue = 1
        for _, exp, v in factors_int:
            expected_residue *= ((v % k) ** exp) % k
        expected_residue %= k
        actual_residue = pure_int % k
        if expected_residue != actual_residue:
            k_z3 = IntVal(k)
            conditions = []
            for root, _, val in factors_int:
                factor_residue = val % k
                condition = root % k_z3 == IntVal(factor_residue)
                if factor_residue == 0:
                    assert 0 == expected_residue
                    conditions = [condition]
                    break
                conditions.append(condition)
            ax = Implies(mk_and(*conditions), pure % k_z3 == IntVal(expected_residue))
            return [ax]
    return []


def project_y(
    x: ArithRef,
    x_exp: int,
    y: ArithRef,
    y_exp: int,
    y_val: IntNumRef,
    pure_xm: ArithRef,
    pure_res: ArithRef,
):
    """Return bound axioms for x^x_exp * y^y_exp when y is fixed to y_val.

    Returns a list of (condition, lhs, rhs) triples, each encoding
    condition => lhs <= rhs, bounding pure_res (which abstracts
    x^x_exp * y^y_exp) in terms of pure_xm (which abstracts x^x_exp).

    When x_exp is even, x^x_exp is always non-negative so the bound direction
    depends only on the sign of y_val^y_exp. When x_exp is odd, x can be negative,
    so we case-split on the sign of x as well.
    """
    lb_cond, lb_bound = const_lb_pow(y, y_exp, y_val)
    ub_cond, ub_bound = const_ub_pow(y, y_exp, y_val)
    if x_exp % 2 == 0:
        if not is_negative_power(y_val, y_exp):
            return [
                (lb_cond, lb_bound * pure_xm, pure_res),
                (ub_cond, pure_res, ub_bound * pure_xm),
            ]
        assert is_negative_power(y_val, y_exp)
        return [
            (ub_cond, pure_res, ub_bound * pure_xm),
            (lb_cond, lb_bound * pure_xm, pure_res),
        ]
    assert x_exp % 2 != 0
    if is_negative_power(y_val, y_exp):
        return [
            (mk_and(x > ZERO, ub_cond), pure_res, ub_bound * pure_xm),
            (mk_and(x < ZERO, ub_cond), ub_bound * pure_xm, pure_res),
        ]
    assert not is_negative_power(y_val, y_exp)
    return [
        (mk_and(x > ZERO, lb_cond), lb_bound * pure_xm, pure_res),
        (mk_and(x < ZERO, lb_cond), pure_res, lb_bound * pure_xm),
    ]


def test():
    def mk_ax_comb_lb(c, x, m, y, n, b):
        return Implies(c, mk_pow(x, m) * mk_pow(y, n) >= b)

    def mk_ax_comb_ub(c, x, m, y, n, b):
        return Implies(c, mk_pow(x, m) * mk_pow(y, n) <= b)

    def mk_ax_pow_ub(c, x, n, b):
        return Implies(c, mk_pow(x, n) <= b)

    def mk_ax_pow_lb(c, x, n, b):
        return Implies(c, mk_pow(x, n) >= b)

    def test_comb_ax(ax, x, y):
        ok = True
        for valx in range(-30, 30, 3):
            for valy in range(-30, 30, 3):
                s = [(x, IntVal(valx)), (y, IntVal(valy))]
                simpl = simplify(substitute(ax, *s))
                ok = is_true(simpl)
                if not ok:
                    print(f"failure on {x}={valx} {y}={valy}, {ax}: {simpl}")
                    break
            if not ok:
                break
        assert ok

    def test_pow_ax(ax, x):
        ok = True
        for val in range(-30, 30, 3):
            ok = ok and is_true(simplify(substitute(ax, (x, IntVal(val)))))
        assert ok

    x = Int("x")
    y = Int("y")
    test_cases = [(1, 2), (1, 3), (2, 3), (2, -3), (3, 3), (3, -3)]
    for n, val_int in test_cases:
        val = IntVal(val_int)
        print("===", n, val)
        c, b = const_lb_pow(x, n, val)
        print(f"clb: {c} -> {x}^{n} >= {b}")
        test_pow_ax(mk_ax_pow_lb(c, x, n, b), x)
        c, b = const_ub_pow(x, n, val)
        print(f"cub: {c} -> {x}^{n} <= {b}")
        test_pow_ax(mk_ax_pow_ub(c, x, n, b), x)

        c, b = lin_lb_pow(x, n, val)
        print(f"llb: {c} -> {x}^{n} >= {b}")
        test_pow_ax(mk_ax_pow_lb(c, x, n, b), x)
        c, b = lin_ub_pow(x, n, val)
        print(f"lub: {c} -> {x}^{n} <= {b}")
        test_pow_ax(mk_ax_pow_ub(c, x, n, b), x)
    for (m, x_int), (n, y_int) in product(test_cases, test_cases):
        x_val = IntVal(x_int)
        y_val = IntVal(y_int)
        print(f"+ comblb: {x}^{m}:{x_val} {y}^{n}:{y_val}")
        for c, b in combine_lb(x, m, x_val, y, n, y_val):
            print(f"comblb: {c} -> {x}^{m}{y}^{n} >= {b}")
            test_comb_ax(mk_ax_comb_lb(c, x, m, y, n, b), x, y)
        print(f"+ combub: {x}^{m}:{x_val} {y}^{n}:{y_val}")
        for c, b in combine_ub(x, m, x_val, y, n, y_val):
            print(f"combub: {c} -> {x}^{m}{y}^{n} <= {b}")
            test_comb_ax(mk_ax_comb_ub(c, x, m, y, n, b), x, y)
    for m in range(1, 5):
        for n, y_int in test_cases:
            y_val = IntVal(y_int)
            xm = mk_pow(x, m)
            res = mk_mul(xm, mk_pow(y, n))
            print(f"projy: x^{m}y^{n} y:{y_val}")
            for tr in project_y(x, m, y, n, y_val, xm, res):
                ax = triple_to_axiom(*tr)
                print(f"projy: {ax}")
                test_comb_ax(ax, x, y)


if __name__ == "__main__":
    test()
