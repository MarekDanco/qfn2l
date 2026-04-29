"""Quantified NIA formula generator with shadowing and negation."""

import argparse
import random
import string

# Default probability of introducing Bool-sorted quantified variables
DEFAULT_BOOL_VAR_PROB = 0.08

# Default probability of using operator chaining for comparison operators
DEFAULT_CHAIN_PROB = 0.15

# Default probability of generating ITE (if-then-else) expressions
DEFAULT_ITE_PROB = 0.1


def rand_var_name() -> str:
    return random.choice(string.ascii_lowercase)


def rand_int() -> int:
    return random.randint(-5, 5)


def gen_simple_term(vars: list[str], depth: int):
    """Generate a simple integer term without ITE (to avoid recursion)."""
    if depth <= 0 or not vars or random.random() < 0.3:
        if vars and random.random() < 0.7:
            return random.choice(vars)
        return str(rand_int())

    op = random.choice(["+", "-", "*"])
    return f"({op} {gen_simple_term(vars, depth - 1)} {gen_simple_term(vars, depth - 1)})"


def gen_simple_atom(int_vars: list[str], bool_vars: list[str] | None = None):
    """Generate a simple atomic formula without ITE (to avoid recursion)."""
    if bool_vars is None:
        bool_vars = []

    # With some probability, just return a boolean variable if available
    if bool_vars and random.random() < 0.3:
        return random.choice(bool_vars)

    op = random.choice(["<", "<=", "=", ">=", ">"])
    lhs = gen_simple_term(int_vars, depth=2)
    rhs = gen_simple_term(int_vars, depth=2)
    return f"({op} {lhs} {rhs})"


def gen_nia_term(opts, vars: list[str], depth: int, bool_vars: list[str] | None = None):
    """Generate arbitrary integer arithmetic terms (non-linear allowed)."""
    if bool_vars is None:
        bool_vars = []

    if depth <= 0 or not vars or random.random() < 0.3:
        if vars and random.random() < 0.7:
            return random.choice(vars)
        return str(rand_int())

    r = random.random()

    ite_prob = getattr(opts, "iteprob", DEFAULT_ITE_PROB)

    # ITE (if-then-else) for integer terms
    if r < ite_prob:
        # Generate a simple boolean condition to avoid deep recursion
        cond = gen_simple_atom(vars, bool_vars)
        t1 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        t2 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        return f"(ite {cond} {t1} {t2})"

    r -= ite_prob

    addprob = (1 - opts.mdprob - opts.mulprob - ite_prob) * 0.75
    # Addition or subtraction
    if r < addprob:
        op = random.choice(["+", "-"])
        t1 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        t2 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        return f"({op} {t1} {t2})"

    r -= addprob

    # Mod or div
    if r < opts.mdprob:
        op = random.choice(["mod", "div"])
        t1 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        t2 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        return f"({op} {t1} {t2})"

    r -= opts.mdprob

    # Multiplication (true non-linear)
    if r < opts.mulprob:
        t1 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        t2 = gen_nia_term(opts, vars, depth - 1, bool_vars)
        return f"(* {t1} {t2})"

    # Coefficiented term
    v = gen_nia_term(opts, vars, depth - 1, bool_vars)
    c = rand_int()
    return f"(* {c} {v})"


def gen_atom(opts, int_vars: list[str], bool_vars: list[str] | None = None):
    """Generate an atomic formula.

    With small probability, use operator chaining for comparison operators.
    Chainable operators in SMT2: <, <=, =, >=, >
    E.g., (< a b c) means (and (< a b) (< b c))
    """
    if bool_vars is None:
        bool_vars = []

    chain_prob = getattr(opts, "chainprob", DEFAULT_CHAIN_PROB)

    # With some probability, just return a boolean variable if available
    if bool_vars and random.random() < 0.3:
        return random.choice(bool_vars)

    op = random.choice(["<", "<=", "=", ">=", ">"])

    # Decide whether to use chaining
    if random.random() < chain_prob:
        # Chain length: 3 or 4 terms
        chain_len = random.randint(3, 4)
        terms = [gen_nia_term(opts, int_vars, depth=3, bool_vars=bool_vars) for _ in range(chain_len)]
        return f"({op} {' '.join(terms)})"

    lhs = gen_nia_term(opts, int_vars, depth=3, bool_vars=bool_vars)
    rhs = gen_nia_term(opts, int_vars, depth=3, bool_vars=bool_vars)
    return f"({op} {lhs} {rhs})"


def gen_bool_term(opts, bool_vars: list[str], depth: int):
    """Generate a boolean term using boolean variables and connectives."""
    if depth <= 0 or not bool_vars or random.random() < 0.5:
        if bool_vars:
            return random.choice(bool_vars)
        return random.choice(["true", "false"])

    chain_prob = getattr(opts, "chainprob", DEFAULT_CHAIN_PROB)

    r = random.random()
    if r < 0.4:
        # Binary boolean connective
        op = random.choice(["and", "or", "=>", "xor"])
        t1 = gen_bool_term(opts, bool_vars, depth - 1)
        t2 = gen_bool_term(opts, bool_vars, depth - 1)
        return f"({op} {t1} {t2})"
    elif r < 0.6:
        # Negation
        t = gen_bool_term(opts, bool_vars, depth - 1)
        return f"(not {t})"
    elif r < 0.8:
        # Chained equality on booleans: (= b1 b2 b3)
        if len(bool_vars) >= 2 and random.random() < chain_prob:
            chain_len = min(random.randint(2, 3), len(bool_vars))
            terms = random.sample(bool_vars, chain_len)
            return f"(= {' '.join(terms)})"
        return random.choice(bool_vars) if bool_vars else "true"
    else:
        return random.choice(bool_vars) if bool_vars else "false"


def gen_nia_bool_expr(
    opts,
    int_vars: list[str],
    depth: int,
    shadow_prob=0.4,
    neg_prob=0.25,
    bool_vars: list[str] | None = None,
):
    """Boolean expression generator for NIA with quantifiers.

    Quantifiers may appear anywhere under Boolean connectives.
    Supports both Int and Bool sorted quantified variables.
    """
    if bool_vars is None:
        bool_vars = []

    bool_var_prob = getattr(opts, "boolprob", DEFAULT_BOOL_VAR_PROB)

    if depth <= 0:
        # At leaf, might use a bool term if bool_vars available
        if bool_vars and random.random() < 0.2:
            expr = gen_bool_term(opts, bool_vars, depth=1)
        else:
            expr = gen_atom(opts, int_vars, bool_vars)
        if random.random() < neg_prob:
            return f"(not {expr})"
        return expr

    r = random.random()

    # Atomic (possibly involving bool vars)
    if r < 0.25:
        if bool_vars and random.random() < 0.25:
            expr = gen_bool_term(opts, bool_vars, depth=2)
        else:
            expr = gen_atom(opts, int_vars, bool_vars)
        if random.random() < neg_prob:
            return f"(not {expr})"
        return expr

    # Boolean connectives (potentially chained for and/or)
    if r < 0.55:
        op = random.choice(["and", "or"])
        n = random.randint(2, 3)
        subs = [
            gen_nia_bool_expr(
                opts, int_vars, depth - 1, shadow_prob, neg_prob, bool_vars
            )
            for _ in range(n)
        ]
        expr = f"({op} {' '.join(subs)})"
        if random.random() < neg_prob:
            return f"(not {expr})"
        return expr

    # Negation
    if r < 0.65:
        sub = gen_nia_bool_expr(
            opts, int_vars, depth - 1, shadow_prob, neg_prob, bool_vars
        )
        return f"(not {sub})"

    # ITE (if-then-else) for boolean expressions
    ite_prob = getattr(opts, "iteprob", DEFAULT_ITE_PROB)
    if r < 0.65 + ite_prob:
        cond = gen_nia_bool_expr(
            opts, int_vars, depth - 1, shadow_prob, neg_prob, bool_vars
        )
        t1 = gen_nia_bool_expr(
            opts, int_vars, depth - 1, shadow_prob, neg_prob, bool_vars
        )
        t2 = gen_nia_bool_expr(
            opts, int_vars, depth - 1, shadow_prob, neg_prob, bool_vars
        )
        expr = f"(ite {cond} {t1} {t2})"
        if random.random() < neg_prob:
            return f"(not {expr})"
        return expr

    # Quantifier introduction (possibly shadowing)
    q = random.choice(["forall", "exists"])
    nvars = random.randint(1, 2)

    new_int_vars = list(int_vars)
    new_bool_vars = list(bool_vars)
    decls = []

    for _ in range(nvars):
        # Decide sort: Int or Bool (Bool with small probability)
        if random.random() < bool_var_prob:
            sort = "Bool"
            target_vars = bool_vars
            new_target_vars = new_bool_vars
        else:
            sort = "Int"
            target_vars = int_vars
            new_target_vars = new_int_vars

        if target_vars and random.random() < shadow_prob:
            v = random.choice(target_vars)  # shadow existing name
        else:
            v = rand_var_name()

        decls.append(f"({v} {sort})")
        new_target_vars.append(v)

    body = gen_nia_bool_expr(
        opts, new_int_vars, depth - 1, shadow_prob, neg_prob, new_bool_vars
    )
    expr = f"({q} ({' '.join(decls)}) {body})"

    if random.random() < neg_prob:
        return f"(not {expr})"
    return expr


def generate_nia_formula(opts, max_depth: int = 4):
    """Top-level NIA formula generator."""
    body = gen_nia_bool_expr(opts, [], max_depth)

    return f"""
(set-logic NIA)
(assert {body})
(check-sat)
""".strip()


def main():
    parser = argparse.ArgumentParser(description="NIA generator")
    parser.add_argument(
        "--mulprob",
        type=float,
        default=0.6,
        help="probability for mul operations",
    )
    parser.add_argument(
        "--mdprob",
        type=float,
        default=0,
        help="probability for mod/div operations",
    )
    parser.add_argument(
        "--boolprob",
        type=float,
        default=DEFAULT_BOOL_VAR_PROB,
        help="probability for introducing Bool-sorted quantified variables",
    )
    parser.add_argument(
        "--chainprob",
        type=float,
        default=DEFAULT_CHAIN_PROB,
        help="probability for using operator chaining in comparisons",
    )
    parser.add_argument(
        "--iteprob",
        type=float,
        default=DEFAULT_ITE_PROB,
        help="probability for generating ITE (if-then-else) expressions",
    )
    args = parser.parse_args()
    print(generate_nia_formula(args))


if __name__ == "__main__":
    main()
