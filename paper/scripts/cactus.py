#!/usr/bin/env python3
"""Generate a cactus plot from benchmark logs.

Run from the repo root:
    python3 paper/scripts/cactus.py
"""

import os
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

LOGS_DIR = Path(__file__).parent.parent.parent / "logs"
OUT_PDF  = Path(__file__).parent.parent / "figures" / "cactus.pdf"

# (label, color, linewidth, linestyle, marker)
SOLVERS = {
    "qfn2l-pa2--frontier":   ("qfn2l (frontier)",  "#e41a1c", 2.2, "-",  "o"),
    # "qfn2l-pa2":             ("qfn2l",             "#ff7f00", 2.2, "-",  "s"),
    # "qfn2l-pa2--congruence": ("qfn2l (congr.)",    "#984ea3", 1.4, "--", "^"),
    "z3":                    ("Z3",                "#377eb8", 1.4, "--", "s"),
    "mathsat":               ("MathSAT",           "#4daf4a", 1.4, "-.", "^"),
    "yices":                 ("Yices",             "#a65628", 1.4, ":",  "D"),
    "cvc5":                  ("cvc5",              "#f781bf", 1.4, "--", "v"),
}

TIMEOUT = 180.0

# ---------------------------------------------------------------------------
# Data collection
# ---------------------------------------------------------------------------

def parse_time(w_path: Path) -> float | None:
    try:
        text = w_path.read_text()
        m = re.search(r"Real time \(s\):\s+([\d.]+)", text)
        return float(m.group(1)) if m else None
    except OSError:
        return None

def is_solved(sol_path: Path) -> bool:
    try:
        content = sol_path.read_text()
        return "sat" in content or "unsat" in content
    except OSError:
        return False

def collect_times(solver_key: str) -> list[float]:
    times = []
    pattern = f"*_{solver_key}"
    for log_dir in sorted(LOGS_DIR.glob(pattern)):
        for w_file in log_dir.glob("*.w"):
            sol_file = w_file.with_suffix("")  # removes .w → .smt2
            # sol file is <name>.smt2.sol
            sol_file = w_file.parent / (w_file.stem + ".sol")
            if not is_solved(sol_file):
                continue
            t = parse_time(w_file)
            if t is not None:
                times.append(t)
    return times

# ---------------------------------------------------------------------------
# Plot
# ---------------------------------------------------------------------------

def main():
    fig, ax = plt.subplots(figsize=(5.5, 3.5))
    legend_handles = []

    for solver_key, (label, color, lw, ls, marker) in SOLVERS.items():
        times = collect_times(solver_key)
        if not times:
            print(f"  WARNING: no data for {solver_key}")
            continue
        times_sorted = sorted(times)
        xs = [0.0] + times_sorted + [TIMEOUT]
        ys = [0]   + list(range(1, len(times_sorted) + 1)) + [len(times_sorted)]
        ax.step(xs, ys, where="post", label=f"{label} ({len(times_sorted)})",
                color=color, linewidth=lw, linestyle=ls)
        # markers at fixed time positions so they spread evenly across x-axis
        import bisect
        marker_times = range(15, int(TIMEOUT) + 1, 15)
        mxs = [t for t in marker_times]
        mys = [bisect.bisect_right(times_sorted, t) for t in mxs]
        ax.plot(mxs, mys, marker=marker, linestyle="none",
                color=color, markersize=4)
        legend_handles.append(Line2D([0], [0], color=color, linewidth=lw,
                                     linestyle=ls, marker=marker, markersize=4,
                                     label=f"{label} ({len(times_sorted)})"))
        print(f"  {solver_key}: {len(times_sorted)} solved")

    ax.set_xlabel("Wall-clock time limit (s)", fontsize=8)
    ax.set_ylabel("Instances solved", fontsize=8)
    ax.tick_params(axis="both", labelsize=8)
    ax.set_xlim(0, TIMEOUT)
    ax.set_ylim(0)
    ax.grid(True, linewidth=0.4, alpha=0.6)
    ax.legend(handles=legend_handles, loc="lower right", fontsize=8, framealpha=0.9)
    fig.tight_layout()

    OUT_PDF.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT_PDF, bbox_inches="tight")
    print(f"Saved: {OUT_PDF}")

if __name__ == "__main__":
    main()
