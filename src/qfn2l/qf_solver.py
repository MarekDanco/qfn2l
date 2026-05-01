#!/usr/bin/env python3
"""Quantifier-free NIA solver."""

import argparse
import os
import signal
import sys
import time

import lia_abstraction
import tagged_logging
import z3
from converter import NNFConverter
from level_info import FormulaInfo
from lia_abstraction import LiaAbstraction
from prefix import QLev
from stats import STATS
from utils import mk_and
from visitors import MakeDefs, SimplePropagate, SimpleSimplify

LOG_TAG = "slv"
log = tagged_logging.mk_logfn(LOG_TAG)


_start_time = 0.0
_brief_stats = False


def _handle_alarm(_signum, _frame):
    STATS.commit_phases()
    STATS.total_time += time.time() - _start_time
    STATS.brief_prn() if _brief_stats else STATS.prn()
    print()
    print("unknown")
    sys.stdout.flush()
    os._exit(0)


class QfSolver:
    def __init__(self, opts, formula):
        self.opts = opts
        STATS.begin_phase(STATS.nnf_time)
        f = NNFConverter()(formula)
        STATS.end_phase()
        STATS.begin_phase(STATS.simplify_time)
        f = SimpleSimplify()(f)
        STATS.end_phase()
        STATS.begin_phase(STATS.propagate_time)
        f = SimplePropagate()(f)
        STATS.end_phase()
        free_vars = list(z3.z3util.get_vars(f))
        prefix = [QLev(is_forall=False, vs=free_vars)]
        STATS.begin_phase(STATS.makedefs_time)
        prefix, f = MakeDefs().make(prefix, f)
        STATS.end_phase()
        STATS.begin_phase(STATS.simplify_time)
        f = SimpleSimplify()(f)
        STATS.end_phase()
        log(3, "input:", prefix, f)
        self.level_info = FormulaInfo(prefix, f)
        self.abstraction = LiaAbstraction(opts, self.level_info, is_exists=True)

    def solve(self) -> bool | None:
        """Returns True (sat), False (unsat), or None (unknown/timeout)."""
        self.abstraction.set_level(0, {})
        while True:
            if self.opts.maxits >= 0 and STATS.its >= self.opts.maxits:
                return None
            STATS.its += 1
            log(1, "it:", STATS.its)
            try:
                model = self.abstraction.solve()
            except lia_abstraction.LIAFail:
                return None
            log(2, "model:", model)
            if model is None:
                return False
            orig_vars = self.level_info.prefix[0].vars()
            assignment = {v: model.eval(v) for v in orig_vars}
            nia_ok = self.abstraction.check_nia(z3.Model(eval=assignment))
            log(2, "nia ok:", nia_ok)
            if nia_ok:
                return True
            self.abstraction.set_level(0, {})


def _print_model(s: QfSolver, orig_consts):
    model = s.abstraction.current_model
    if model is None:
        return
    print(";; model-start")
    for decl in model.decls():
        if decl not in orig_consts:
            continue
        val = model.eval(decl(), model_completion=True)
        print(f"(define-fun {decl.name()} () Int {val})")
    print(";; model-end")


def handle_shutdown(signum, _):
    print("\nShutdown signal received:", signum)
    STATS.commit_phases()
    STATS.total_time += time.time() - _start_time
    STATS.brief_prn() if _brief_stats else STATS.prn()
    sys.stdout.flush()
    os._exit(0)


def z3_preprocess(formula):
    t = z3.Then("simplify", "propagate-values", "solve-eqs", "simplify")
    g = z3.Goal()
    g.add(formula)
    return t(g).as_expr()


def z3_preprocess_aggressive(formula, level=1, timeout=5000):
    all_vars = list(z3.z3util.get_vars(formula))
    tactics = [
        ("simplify[1]", z3.With("simplify", arith_lhs=True, hoist_mul=True, som=True)),
        (
            "propagate-values",
            z3.With(
                "propagate-values",
                local_ctx=True,
                arith_lhs=True,
                rewrite_patterns=True,
            ),
        ),
        ("propagate-ineqs", z3.With("propagate-ineqs")),
        ("normalize-bounds", z3.With("normalize-bounds")),
        ("solve-eqs", z3.With("solve-eqs", context_solve=True)),
        ("simplify[2]", z3.With("simplify", arith_lhs=True, hoist_mul=True)),
        ("ctx-simplify", z3.With("ctx-simplify")),
        ("simplify[3]", z3.With("simplify", arith_lhs=True, hoist_mul=True)),
    ]
    if level > 1:
        tactics += [
            ("ctx-solver-simplify", z3.With("ctx-solver-simplify")),
            ("simplify[4]", z3.With("simplify", arith_lhs=True, hoist_mul=True)),
        ]
    current = formula
    for name, tactic in tactics:
        try:
            g = z3.Goal()
            g.add(current)
            current = z3.TryFor(tactic, timeout)(g).as_expr()
            log(2, f"tactic {name} succeeded")
            log(5, f"formula after {name}:", current)
        except Exception:
            log(2, f"tactic {name} failed or timed out")
    # new_vars = list(z3.z3util.get_vars(current))
    # assert set(new_vars).issubset(set(all_vars)), (
    #     "preprocessing introduced new variables"
    # )
    return current


def main():
    signal.signal(signal.SIGTERM, handle_shutdown)
    signal.signal(signal.SIGINT, handle_shutdown)

    parser = argparse.ArgumentParser(description="QF_NIA solver.")
    parser.add_argument("filename", default="-", nargs="?")
    parser.add_argument(
        "-v", dest="verbose", default=0, type=int, help="verbosity level"
    )
    parser.add_argument(
        "--maxits",
        default=-1,
        type=int,
        help="maximum number of iterations (returns unknown)",
    )
    parser.add_argument(
        "--modax",
        default=2,
        type=int,
        help="apply modulo axioms up to this value (<=1 disables)",
    )
    parser.add_argument(
        "--bounds",
        default=False,
        action=argparse.BooleanOptionalAction,
        help="use heuristic bounds on the LIA solver",
    )
    parser.add_argument(
        "--zeros",
        default=False,
        action=argparse.BooleanOptionalAction,
        help="try setting multiplication variables to 0",
    )
    parser.add_argument(
        "--static",
        default=False,
        action=argparse.BooleanOptionalAction,
        help="add static axioms for div/mod",
    )
    parser.add_argument("--seed", default=7, type=int, help="z3 random seed")
    parser.add_argument(
        "-p",
        "--preprocess",
        default=False,
        action=argparse.BooleanOptionalAction,
        help="preprocess with z3 tactics",
    )
    parser.add_argument(
        "-pa",
        "--preprocess-aggressive",
        default=0,
        type=int,
        help="aggressive preprocessing level",
    )
    parser.add_argument(
        "-pat",
        "--preprocess-aggressive-timeout",
        default=5000,
        type=int,
        help="timeout for aggressive preprocessing tactics in ms",
    )
    parser.add_argument(
        "--heur-timeout",
        default=3000,
        dest="heur_to",
        type=int,
        help="timeout for heuristic LIA calls in ms",
    )
    parser.add_argument(
        "--timeout",
        default=-1,
        type=float,
        help="wall-clock timeout in seconds (-1 = no limit)",
    )
    parser.add_argument(
        "--print-model",
        dest="print_model",
        default=False,
        action=argparse.BooleanOptionalAction,
        help="print SAT model as SMT2 define-fun lines",
    )
    parser.add_argument(
        "--recursion-depth",
        dest="recursion_depth",
        default=10000,
        type=int,
        help="Python recursion limit",
    )
    parser.add_argument(
        "--brief-stats",
        dest="brief_stats",
        default=False,
        action=argparse.BooleanOptionalAction,
        help="on exit print only: terminated phase, longest phase, iteration count, pures count",
    )

    opts = parser.parse_args()
    sys.setrecursionlimit(opts.recursion_depth)
    global _start_time, _brief_stats
    _brief_stats = opts.brief_stats
    opts.start_time = time.time()
    _start_time = opts.start_time
    tagged_logging.VERBOSITY_LEVELS[LOG_TAG] = opts.verbose
    tagged_logging.VERBOSITY_LEVELS[lia_abstraction.LOG_TAG] = opts.verbose

    if opts.timeout > 0:
        signal.signal(signal.SIGALRM, _handle_alarm)
        signal.setitimer(signal.ITIMER_REAL, opts.timeout)

    z3.set_param("smt.random_seed", opts.seed)
    z3.set_param("sat.random_seed", opts.seed)

    s = z3.Solver()
    STATS.begin_phase(STATS.parse_time)
    if opts.filename == "-":
        s.from_string(sys.stdin.read())
    else:
        s.from_file(opts.filename)
    formula = mk_and(*s.assertions())
    STATS.end_phase()

    orig_consts = set()
    if opts.print_model:
        orig_consts = {v.decl() for v in z3.z3util.get_vars(formula)}

    solver = None
    res = None
    try:
        if opts.preprocess_aggressive > 0:
            formula = z3_preprocess_aggressive(
                formula,
                level=opts.preprocess_aggressive,
                timeout=opts.preprocess_aggressive_timeout,
            )
        elif opts.preprocess:
            formula = z3_preprocess(formula)

        STATS.begin_phase(STATS.init_time)
        solver = QfSolver(opts, formula)
        STATS.end_phase()

        res = solver.solve()
    finally:
        if opts.timeout > 0:
            signal.setitimer(signal.ITIMER_REAL, 0)

    STATS.total_time += time.time() - opts.start_time
    STATS.brief_prn() if opts.brief_stats else STATS.prn()
    print()
    if res is None:
        print("unknown")
    elif res:
        if opts.print_model and solver is not None:
            _print_model(solver, orig_consts)
        print("sat")
    else:
        print("unsat")


if __name__ == "__main__":
    main()
