#!/usr/bin/env python3
"""Verify qfn2l-cpp SAT answers by substituting the model into the original formula.

Usage:
    check_model_cpp.py [options] FILE [-- SOLVER-OPTIONS...]

The solver is invoked with --print-model. If it returns sat, the model values
are added as (assert (= var val)) constraints and the resulting file is passed
to a reference solver. If the reference solver returns sat, the model is valid.

"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

DEFAULT_SOLVER = "qfn2l-cpp"


def run_solver(filename, solver_args, solver_bin):
    cmd = [solver_bin, "--print-model"] + solver_args + [filename]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.stdout


def parse_output(output):
    """Return (verdict, model_lines) from solver stdout.

    verdict is 'sat', 'unsat', 'unknown', or None if not found.
    model_lines is a list of (define-fun ...) strings.
    """
    lines = output.splitlines()
    verdict = None
    model_lines = []
    in_model = False
    for line in lines:
        stripped = line.strip()
        if stripped in ("sat", "unsat", "unknown"):
            verdict = stripped
        elif stripped == ";; model-start":
            in_model = True
        elif stripped == ";; model-end":
            in_model = False
        elif in_model and stripped.startswith("(define-fun"):
            model_lines.append(stripped)
    return verdict, model_lines


# Matches both positive (42) and z3-style negative ((- 42)) integer values.
_DEFINE_FUN_RE = re.compile(
    r"\(define-fun\s+(\S+)\s+\(\)\s+Int\s+((?:\(- \d+\)|\d+))\)"
)


def model_to_assertions(model_lines):
    """Convert (define-fun x () Int val) lines to (assert (= x val)) lines."""
    assertions = []
    for line in model_lines:
        m = _DEFINE_FUN_RE.match(line)
        if m:
            name, value = m.group(1), m.group(2)
            assertions.append(f"(assert (= {name} {value}))")
        else:
            print(f"  WARNING: could not parse model line: {line}", file=sys.stderr)
    return assertions


def create_verification_smt2(original_file, assertions):
    with open(original_file) as f:
        content = f.read()
    last = content.rfind("(check-sat)")
    if last == -1:
        return content + "\n" + "\n".join(assertions) + "\n(check-sat)\n"
    return content[:last] + "\n".join(assertions) + "\n" + content[last:]


def run_ref_solver(smt2_content, ref_solver, keep_tmp):
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".smt2", delete=False, prefix="checkmodel_cpp_"
    ) as f:
        f.write(smt2_content)
        tmp_path = f.name
    try:
        proc = subprocess.run(
            [ref_solver, tmp_path], capture_output=True, text=True, timeout=30
        )
        verdict = next((l.strip() for l in proc.stdout.splitlines() if l.strip()), None)
        return verdict, tmp_path
    except subprocess.TimeoutExpired:
        return "timeout", tmp_path
    finally:
        if not keep_tmp and os.path.exists(tmp_path):
            os.unlink(tmp_path)


def main():
    if "--" in sys.argv:
        sep = sys.argv.index("--")
        checker_argv = sys.argv[1:sep]
        solver_args = sys.argv[sep + 1 :]
    else:
        checker_argv = sys.argv[1:]
        solver_args = []

    parser = argparse.ArgumentParser(
        description="Verify qfn2l-cpp SAT answers against a reference solver.",
    )
    parser.add_argument("file", help="SMT2 input file")
    parser.add_argument(
        "--ref-solver",
        default="z3",
        metavar="CMD",
        help="reference solver binary (default: z3)",
    )
    parser.add_argument(
        "--solver",
        default=DEFAULT_SOLVER,
        metavar="CMD",
        help=f"solver binary (default: {DEFAULT_SOLVER})",
    )
    parser.add_argument(
        "--keep-tmp",
        action="store_true",
        help="keep the temporary verification SMT2 file",
    )
    args = parser.parse_args(checker_argv)

    print(f"[check] {args.file}", end="  ", flush=True)
    if solver_args:
        print(f"(solver args: {' '.join(solver_args)})", end="  ", flush=True)

    output = run_solver(args.file, solver_args, args.solver)
    verdict, model_lines = parse_output(output)

    if verdict != "sat":
        print(f"-> {verdict or 'no-verdict'} (skip)")
        return 0

    if not model_lines:
        print("-> sat (no model printed, cannot verify)")
        return 0

    assertions = model_to_assertions(model_lines)
    smt2 = create_verification_smt2(args.file, assertions)
    ref_verdict, tmp_path = run_ref_solver(smt2, args.ref_solver, args.keep_tmp)

    if ref_verdict == "sat":
        print("-> sat OK")
        return 0
    else:
        print(f"-> FAIL (ref solver: {ref_verdict})")
        if args.keep_tmp:
            print(f"  verification file: {tmp_path}")
        else:
            with tempfile.NamedTemporaryFile(
                mode="w", suffix=".smt2", delete=False, prefix="checkmodel_cpp_FAIL_"
            ) as f:
                f.write(smt2)
                print(f"  verification file saved: {f.name}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
