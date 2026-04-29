#!/usr/bin/env python3

import argparse
import os
import random
import string
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
from typing import Optional

###############################################################################
# Utilities
###############################################################################


def run_solver(cmd: str, smt2: str, timeout: int = 120) -> Optional[str]:
    """Run solver with SMT-LIB input via stdin.

    Returns 'sat', 'unsat', or None (unknown / malformed / timeout).
    """
    try:
        proc = subprocess.run(
            cmd,
            input=smt2,
            text=True,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None

    for l in proc.stdout.splitlines():
        l = l.strip().lower()
        if l == "sat" or l == "unsat":
            return l

    return None


def rand_var():
    return random.choice(string.ascii_lowercase)


def rand_int():
    return random.randint(-20, 20)


###############################################################################
# Improved quantified LIA formula generator
###############################################################################

###############################################################################
# Quantified LIA generator with variable shadowing
###############################################################################


def rand_var_name():
    return random.choice(string.ascii_lowercase)


def gen_linear_term(vars, depth):
    if depth <= 0 or not vars or random.random() < 0.4:
        if vars and random.random() < 0.7:
            return random.choice(vars)
        return str(rand_int())

    op = random.choice(["+", "-"])
    t1 = gen_linear_term(vars, depth - 1)
    t2 = gen_linear_term(vars, depth - 1)
    return f"({op} {t1} {t2})"


def gen_atom(vars):
    op = random.choice(["<", "<=", "=", ">=", ">"])
    lhs = gen_linear_term(vars, depth=2)
    rhs = gen_linear_term(vars, depth=2)
    return f"({op} {lhs} {rhs})"


def gen_bool_expr(vars, depth, shadow_prob=0.4):
    """
    vars: list of currently in-scope variable names (with shadowing allowed)
    """
    if depth <= 0:
        return gen_atom(vars)

    r = random.random()

    # Atomic
    if r < 0.3:
        return gen_atom(vars)

    # Boolean connectives
    if r < 0.6:
        op = random.choice(["and", "or"])
        n = random.randint(2, 3)
        subs = [gen_bool_expr(vars, depth - 1, shadow_prob) for _ in range(n)]
        return f"({op} {' '.join(subs)})"

    if r < 0.75:
        sub = gen_bool_expr(vars, depth - 1, shadow_prob)
        return f"(not {sub})"

    # Quantifier (possibly shadowing)
    q = random.choice(["forall", "exists"])
    nvars = random.randint(1, 3)

    new_vars = list(vars)
    decls = []

    for _ in range(nvars):
        if vars and random.random() < shadow_prob:
            v = random.choice(vars)  # shadow an existing variable
        else:
            v = rand_var_name()
        decls.append(f"({v} Int)")
        new_vars.append(v)

    body = gen_bool_expr(new_vars, depth - 1, shadow_prob)
    return f"({q} ({' '.join(decls)}) {body})"


def generate_lia_formula(max_depth=4):
    body = gen_bool_expr([], max_depth)
    return f"""
(set-logic LIA)
(assert {body})
(check-sat)
""".strip()


###############################################################################
# Differential Test
###############################################################################


def test_once(test_id: int, solver1: str, solver2: str, bugs_dir: str):
    smt = generate_lia_formula()

    r1 = run_solver(solver1, smt)
    r2 = run_solver(solver2, smt)

    if r1 is None or r2 is None:
        return False
    # print(r1, r2)

    if r1 != r2:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        fname = os.path.join(bugs_dir, f"bug_{ts}_{test_id}.smt2")
        with open(fname, "w") as f:
            f.write(smt)
        print(f"[BUG] Disagreement: {r1} vs {r2} -> {fname}")
        return True

    return False


###############################################################################
# Main
###############################################################################


def main():
    parser = argparse.ArgumentParser(description="LIA SMT solver differential fuzzer")
    parser.add_argument("tests", type=int, help="Number of tests")
    parser.add_argument("solver1", help="Solver command S1")
    parser.add_argument("solver2", help="Solver command S2")
    parser.add_argument("-j", type=int, default=1, help="Parallel jobs")
    parser.add_argument("--bugs", default="bugs", help="Bug output directory")

    args = parser.parse_args()

    os.makedirs(args.bugs, exist_ok=True)

    with ThreadPoolExecutor(max_workers=args.j) as executor:
        futures = [
            executor.submit(test_once, i, args.solver1, args.solver2, args.bugs)
            for i in range(args.tests)
        ]

        for _ in as_completed(futures):
            pass


if __name__ == "__main__":
    main()
