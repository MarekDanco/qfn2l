#!/usr/bin/env python3

import argparse
import hashlib
import multiprocessing as mp
import os
import random
import subprocess
import time
from collections import Counter

import nia_gen

# -------------------------------
# Formula generation
# -------------------------------


def fresh_var(i):
    return f"x{i}"


def gen_arith_expr(vars, depth, iteprob=0.1):
    if depth <= 0 or random.random() < 0.3:
        return random.choice(vars + [str(random.randint(-10, 10))])

    if random.random() < iteprob:
        cond = gen_atom(vars)
        t1 = gen_arith_expr(vars, depth - 1, iteprob)
        t2 = gen_arith_expr(vars, depth - 1, iteprob)
        return f"(ite {cond} {t1} {t2})"

    op = random.choice(["+", "-", "*"])
    return f"({op} {gen_arith_expr(vars, depth - 1, iteprob)} {gen_arith_expr(vars, depth - 1, iteprob)})"


def gen_atom(vars, iteprob=0.1):
    lhs = gen_arith_expr(vars, depth=2, iteprob=iteprob)
    rhs = gen_arith_expr(vars, depth=2, iteprob=iteprob)
    pred = random.choice(["=", "<", "<=", ">", ">="])
    return f"({pred} {lhs} {rhs})"


def gen_bool_expr(vars, depth, iteprob=0.1):
    if depth <= 0:
        return gen_atom(vars, iteprob)

    r = random.random()
    if r < 0.35:
        op = random.choice(["and", "or"])
        return (
            f"({op} {gen_bool_expr(vars, depth - 1, iteprob)} {gen_bool_expr(vars, depth - 1, iteprob)})"
        )
    elif r < 0.5:
        return f"(not {gen_bool_expr(vars, depth - 1, iteprob)})"
    elif r < 0.5 + iteprob:
        cond = gen_bool_expr(vars, depth - 1, iteprob)
        t1 = gen_bool_expr(vars, depth - 1, iteprob)
        t2 = gen_bool_expr(vars, depth - 1, iteprob)
        return f"(ite {cond} {t1} {t2})"
    else:
        return gen_atom(vars, iteprob)


def gen_qf_nia_formula(iteprob=0.1):
    n = random.randint(2, 6)
    vars = [fresh_var(i) for i in range(n)]
    decls = "\n".join(f"(declare-const {v} Int)" for v in vars)
    body = gen_bool_expr(vars, depth=3, iteprob=iteprob)

    return f"""(set-logic QF_NIA)
{decls}
(assert {body})
(check-sat)
"""


# -------------------------------
# Solver interaction
# -------------------------------


def run_solver(cmd, formula):
    try:
        p = subprocess.run(
            cmd.split(),
            input=formula.encode(),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        return p.stdout.decode(errors="ignore").lower()
    except Exception:
        return "error"


def classify(output):
    if "unsat" in output:
        return "unsat"
    if "sat" in output:
        return "sat"
    return "unknown"


# -------------------------------
# Single test
# -------------------------------


def run_test(args):
    idx, opts = args
    solver1, solver2 = opts.solver1, opts.solver2
    stats = Counter()

    formula = nia_gen.generate_nia_formula(opts) if opts.nia else gen_qf_nia_formula(opts.iteprob)
    out1 = run_solver(solver1, formula)
    out2 = run_solver(solver2, formula)

    r1 = classify(out1)
    r2 = classify(out2)

    stats["generated"] += 1

    # Per-solver solved counts
    if r1 in {"sat", "unsat"}:
        stats["solver1_solved"] += 1
    else:
        stats["solver1_unknown"] += 1

    if r2 in {"sat", "unsat"}:
        stats["solver2_solved"] += 1
    else:
        stats["solver2_unknown"] += 1

    for i, r in enumerate([r1, r2]):
        if r in {"sat", "unsat", "unknown"}:
            continue
        dirn = f"strange_{i}"
        stats[f"solver{i}_strange"] += 1
        os.makedirs(dirn, exist_ok=True)
        h = hashlib.sha256(formula.encode()).hexdigest()[:16]
        fname = f"{dirn}/strange_{idx}_{h}.smt2"
        with open(fname, "w") as f:
            f.write(formula)

    # Joint comparison
    if opts.diff and r1 != r2:
        dirn = "diffs"
        os.makedirs(dirn, exist_ok=True)
        h = hashlib.sha256(formula.encode()).hexdigest()[:16]
        fname = f"{dirn}/d_{idx}_{h}.smt2"
        with open(fname, "w") as f:
            f.write(formula)

    if r1 in {"sat", "unsat"} and r2 in {"sat", "unsat"}:
        stats["both_solved"] += 1

        if r1 != r2:
            stats["mismatch"] += 1
            os.makedirs("bugs", exist_ok=True)
            h = hashlib.sha256(formula.encode()).hexdigest()[:16]
            fname = f"bugs/bug_{idx}_{h}.smt2"
            with open(fname, "w") as f:
                f.write(formula)
    else:
        stats["at_least_one_unknown"] += 1

    return stats


# -------------------------------
# Main
# -------------------------------


def main():
    parser = argparse.ArgumentParser(description="(QF)NIA differential fuzzer")
    parser.add_argument("num_tests", type=int)
    parser.add_argument("solver1", type=str)
    parser.add_argument("solver2", type=str)
    parser.add_argument("-j", "--jobs", type=int, default=1)
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
        "-d",
        "--diff",
        default=False,
        action=argparse.BooleanOptionalAction,
        type=bool,
        help="keep files that give different results",
    )
    parser.add_argument(
        "-n",
        "--nia",
        default=True,
        action=argparse.BooleanOptionalAction,
        type=bool,
        help="generate full nia problems, including quantifiers",
    )
    parser.add_argument(
        "--boolprob",
        type=float,
        default=0.08,
        help="probability for introducing Bool-sorted quantified variables",
    )
    parser.add_argument(
        "--chainprob",
        type=float,
        default=0.15,
        help="probability for using operator chaining in comparisons",
    )
    parser.add_argument(
        "--iteprob",
        type=float,
        default=0.1,
        help="probability for generating ITE (if-then-else) expressions",
    )
    args = parser.parse_args()

    work = [(i, args) for i in range(args.num_tests)]
    total = Counter()

    start = time.time()

    if args.jobs == 1:
        for w in work:
            total.update(run_test(w))
    else:
        with mp.Pool(args.jobs) as pool:
            for s in pool.imap_unordered(run_test, work):
                total.update(s)

    elapsed = time.time() - start

    # -------------------------------
    # Statistics report
    # -------------------------------

    print("\n=== Fuzzing statistics ===")
    print(f"Total formulas generated        : {total['generated']}")
    print()
    print("Solver-specific:")
    print(f"  Solver 1 solved (sat/unsat)   : {total['solver1_solved']}")
    print(f"  Solver 1 unknown/error        : {total['solver1_unknown']}")
    print(f"  Solver 1 strange res          : {total['solver1_strange']}")
    print(f"  Solver 2 solved (sat/unsat)   : {total['solver2_solved']}")
    print(f"  Solver 2 unknown/error        : {total['solver2_unknown']}")
    print(f"  Solver 2 strange res          : {total['solver2_strange']}")
    print()
    print("Joint comparison:")
    print(f"  Both solvers solved           : {total['both_solved']}")
    print(f"  At least one unknown/error    : {total['at_least_one_unknown']}")
    print(f"  True mismatches               : {total['mismatch']}")
    print()
    print(f"Elapsed time (s)                : {elapsed:.2f}")

    if total["both_solved"] > 0:
        rate = 100.0 * total["mismatch"] / total["both_solved"]
        print(f"Mismatch rate (%)               : {rate:.2f}")


if __name__ == "__main__":
    main()
