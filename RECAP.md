# Recent Work Recap (2026-05-09 – 2026-05-13)

All work was on the C++ solver (`src/qfn2l_cpp/`) unless noted.

## smt-switch patch (local, not committed upstream)

`/home/marek/solvers/smt-switch/z3/src/z3_term.cpp` — `Z3Term::hash()` and `Z3Term::compare()` were using `z3::expr::hash()` / `z3::func_decl::hash()`, which can collide. Changed to `id()` (unique per term/decl in z3). This was the root cause of the congruence hash-collision bug and is a prerequisite for correct operation of any `UnorderedTermMap` or `UnorderedTermSet` keyed on z3 terms.

This patch lives outside the repo; it needs to be reapplied if smt-switch is updated or rebuilt from scratch.

## Correctness fixes

- **Arbitrary-precision numerics** — switched from fixed-width integers to big integers to avoid overflow in bounds/projection arithmetic.
- **Model completion failure misreported as unsat** — fixed a bug where a failed model completion (not a true UNSAT) was returned as UNSAT.
- **Fixed LIA solver strategy** — C++ was using `set_logic("QF_NIA")` instead of a dedicated LIA solver; switched to the right strategy and added a bounded re-check over all variables.
- **Removed `SimplePropagate` from `set_level`** *(uncommitted)* — propagating definition equalities (introduced by `MakeDefs`) back into the body undid the binary factoring and created 3+-factor products that `split_mul` cannot handle. The definitions now stay as plain linear LIA constraints.
- **Fixed bounds heuristic skipped** — `--bounds` was silently skipped when the `--zeros` heuristic failed first.
- **Congruence hash collision** — `_congruence_pairs_added` was keyed on pointer addresses (non-stable); fixed.

## Intermediate pures (binary mul parsing)

smt-switch represents `(* x x x)` as `(* x (* x x))`. The bottom-up Purifier was creating an intermediate pure e_x4 (for x²) and then e_x5 (for x³), leaving e_x4 alive in the prefix with spurious smul axioms that pollute the LIA.

Three-part fix (commit `acd52be`):
1. `Purifier::visit_mul`: flattens by looking through pures-for-muls, creates one canonical pure for x³.
2. `make_pure_constant` smul axioms: use `split_mul` leaves (no NIA subterms in LIA axioms).
3. `set_level`: calls `CollectPures` on `_current_pure_body`; only includes axioms for reachable pures.

Effect on STC_0019 (`--zeros`): 300–470 iterations → ~88.

**Remaining issue:** intermediate pures are still created as free prefix variables (no constraints attached, so harmless but messy). Tracked in `STATUS.md` as open issue #1.

## Adaptive model shrinking

Mirrored Python's `--bounds` heuristic: shrinking is now adaptive (tries smaller bounds on each retry) rather than fixed ±1000.

## Removed `level_info`

Simplified the C++ solver to a single existential quantifier level (matching the QF use case). Removed the `level_info` tracking layer present in the Python version.

## Logging and usability

- Readable pure names: exponent notation (`x^3`) and `_` separator (e.g. `e_x_y^2`).
- `--stats` flag: stats suppressed by default, shown only on `--stats`.
- Print usage when run with no args on a terminal.
- Added LIA formula dump at verbosity 5 *(uncommitted)*.
- Added `print_pures_values()` to log pure assignments.

## Documentation

- `CPP_VS_PYTHON.md`: full gap analysis between C++ and Python solver.
- `STATUS.md`: build instructions, passing test matrix, and known open issues.

## Tests

All six entries in `examples/test_me_cpp.sh` pass (hard.c_2, hard.c_3, hard-ll.c_0..4, STC_0019, STC_0072, STC_0504) across `--zeros`, `--bounds`, and `--zeros --bounds --modax 5`.
