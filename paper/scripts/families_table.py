#!/usr/bin/env python3
"""Generate per-family sat/unsat breakdown table from details_final.txt.

Run from repo root:
    python3 paper/scripts/families_table.py
"""

import re
from pathlib import Path

DETAILS = Path(__file__).parent.parent.parent / "details_final.txt"
OUT_TEX = Path(__file__).parent.parent / "tables" / "families.tex"

SOLVERS_ORDERED = [
    ("qfn2l-pa2--frontier",   r"\solver+\textsc{Fr.}"),
    ("qfn2l-pa2",             r"\solver"),
    ("qfn2l-pa2--congruence", r"\solver+\textsc{Cg.}"),
    ("z3",                    r"Z3"),
    ("mathsat",               r"MathSAT"),
    ("yices",                 r"Yices"),
    ("cvc5",                  r"cvc5"),
]

SKIP_PATTERN = "MathProblems"

QFNL2_KEYS = {"qfn2l-pa2--frontier", "qfn2l-pa2", "qfn2l-pa2--congruence"}
COMPETITOR_KEYS = {"z3", "mathsat", "yices", "cvc5"}


def we_win(fdata):
    qfn2l_best = max(
        (sat + unsat for sk, (_, sat, unsat) in fdata.items() if sk in QFNL2_KEYS),
        default=0,
    )
    comp_best = max(
        (sat + unsat for sk, (_, sat, unsat) in fdata.items() if sk in COMPETITOR_KEYS),
        default=0,
    )
    return qfn2l_best > comp_best


def parse_details(path):
    """Returns dict: family_name -> {solver -> (total, sat, unsat)}"""
    text = path.read_text()
    families = {}
    current = None
    for line in text.splitlines():
        m = re.match(r"^Family:\s+(\S+)", line)
        if m:
            raw = re.sub(r"_180_40000$", "", m.group(1))
            current = raw
            families[current] = {}
            continue
        if current is None:
            continue
        m = re.match(r"^\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)", line)
        if m:
            solver = m.group(1)
            total, sat, unsat = int(m.group(2)), int(m.group(3)), int(m.group(4))
            families[current][solver] = (total, sat, unsat)
    return families


def fmt(n):
    return str(n)


def display_name(family):
    # strip leading YYYYMMDD- date prefix if present
    return re.sub(r"^\d{8}-", "", family)


def main():
    families = parse_details(DETAILS)
    families = {k: v for k, v in families.items() if SKIP_PATTERN not in k}
    # sort by total size descending
    sorted_families = sorted(
        families.items(),
        key=lambda kv: list(kv[1].values())[0][0],
        reverse=True,
    )

    solver_keys = [k for k, _ in SOLVERS_ORDERED]
    solver_labels = [lbl for _, lbl in SOLVERS_ORDERED]

    lines = []
    lines.append(r"\begin{table}[t]")
    lines.append(r"    \centering")
    lines.append(r"    \resizebox{\linewidth}{!}{%")
    lines.append(r"    \begin{tabular}{l | lll | llll}")
    lines.append(r"        \hline")
    lines.append(
        r"        \textbf{Family} & "
        + " & ".join(r"\textbf{" + lbl + r"}" for lbl in solver_labels)
        + r" \\"
    )
    lines.append(r"        \hline")

    for fname, fdata in sorted_families:
        total = list(fdata.values())[0][0]
        label = rf"{display_name(fname)} ({fmt(total)})"
        bold = we_win(fdata)
        cells = []
        for sk in solver_keys:
            if sk in fdata:
                _, sat, unsat = fdata[sk]
                cell = f"{fmt(sat)}/{fmt(unsat)}"
                if bold and sk in QFNL2_KEYS:
                    cell = r"\textbf{" + cell + r"}"
                cells.append(cell)
            else:
                cells.append("---")
        lines.append("        " + label + " & " + " & ".join(cells) + r" \\")

    lines.append(r"        \hline")
    lines.append(r"    \end{tabular}%")
    lines.append(r"    }")
    lines.append(r"    \vspace{0.9em}")
    lines.append(
        r"    \caption{Sat/unsat instances solved per benchmark family within 180\,s"
        r" (excluding \texttt{20220315-MathProblems}, see Table~\ref{tab:results})."
        r"\label{tab:families}}"
    )
    lines.append(r"\end{table}")

    OUT_TEX.parent.mkdir(parents=True, exist_ok=True)
    OUT_TEX.write_text("\n".join(lines) + "\n")
    print(f"Saved: {OUT_TEX}")


if __name__ == "__main__":
    main()
