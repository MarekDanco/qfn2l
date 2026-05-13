# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

qfn2l is a quantifier-free NIA (Nonlinear Integer Arithmetic) solver. It solves QF_NIA SMT2 formulas over integer arithmetic including multiplication, integer division (`div`), and modulo (`mod`).

The solver's approach: abstract nonlinear operations into fresh constants ("pures"), solve the resulting LIA (Linear Integer Arithmetic) abstraction, then check if the NIA semantics are satisfied, adding axioms on failures.

## Running the Solver

The main entry point is `src/qfn2l/qf_solver.py`. SMT2 files are provided as input:

```bash
python3 src/qfn2l/qf_solver.py examples/jain_5-2.c_0.smt2
python3 src/qfn2l/qf_solver.py -v3 examples/hard.c_2.smt2   # verbosity level 3
python3 src/qfn2l/qf_solver.py -p examples/jain_5-2.c_0.smt2    # preprocess with z3 tactics
python3 src/qfn2l/qf_solver.py -pa 1 examples/jain_5-2.c_0.smt2 # aggressive preprocessing level 1
python3 src/qfn2l/qf_solver.py --modax 4 examples/jain_5-2.c_0.smt2  # modulo axioms up to value 4
```

Key options:
- `-v N` — verbosity (default 0; higher = more output; module tags: `[slv]`, `[abs]`)
- `--maxits N` — limit iterations (returns `unknown`)
- `--timeout N` — wall-clock timeout in seconds (float, -1 = no limit); reliable because it sets z3's internal C-level timeout on each LIA call
- `--bounds` / `--zeros` — heuristics for LIA solver
- `--static` — add static axioms for `div`/`mod`
- `--seed N` — set z3 random seed (default 7)
- `--modax N` — modulo axioms up to value N (default 2; <=1 disables)
- `-p` / `--preprocess` — preprocess with z3 tactics
- `-pa N` / `--preprocess-aggressive N` — aggressive preprocessing level
- `-pat N` / `--preprocess-aggressive-timeout N` — timeout for aggressive preprocessing in ms (default 5000)
- `--heur-timeout N` — timeout for heuristic LIA calls in ms (default 3000)
- `--print-model` — print SAT model as SMT2 define-fun lines
- `--stats` — print full stats on exit (default: silent)
- `--brief-stats` — on exit print only: terminated phase, longest phase, iteration count, pures count
- `--recursion-depth N` — set Python recursion limit (default 100000)

Note: shell `timeout` / SIGTERM is **unreliable** for this solver — z3 can block in C code and never deliver the signal to Python. Use `--timeout` instead.

## Architecture

All source is in `src/qfn2l/`. The main modules:

**`qf_solver.py`** — Entry point (`main()`). The `QfSolver` class drives the solving loop:
1. Convert to NNF → simplify → propagate equalities → introduce definitions via `MakeDefs`
2. Build a single-level existential prefix from the free variables
3. Repeat: call `LiaAbstraction.solve()`, check NIA correctness via `check_nia()`, add axioms on failure

**`lia_abstraction.py`** — `LiaAbstraction` class. Core logic for:
- *Purification* (`Purifier` inner class): replaces nonlinear subterms (mul, div, mod with uninterpreted args) with fresh constants called "pures"
- `set_level()`: instantiates the abstraction under the current variable assignment
- `solve()`: calls z3 LIA solver, with optional `--bounds`/`--zeros` heuristics
- `check_nia()`: verifies pure constants match their actual NIA values; adds axioms on mismatch
- Axiom generation: `mk_mul_axioms`, `mk_mod_axiom`, `mk_idiv_axiom`, `mk_pow_axioms`, `mk_mixed_mul_axioms`, `mk_congruence_axioms`

**`prefix.py`** — `QLev` represents one quantifier level (forall or exists, plus a list of variables). Also provides `to_fla()` to reconstruct a quantified z3 formula from a prefix + body.

**`level_info.py`** — `FormulaInfo` tracks which quantifier level each constant/term belongs to.

**`pures.py`** — `Pures` bidirectional map between NIA terms and their pure constants. `CollectPures` traverses a formula collecting active pures. `CheckVal` supports NIA checking.

**`projections.py`** — Linear arithmetic bounds for NIA terms (used in axiom generation): `lin_lb_pow`, `lin_ub_pow`, `combine_lb`, `combine_ub`, `mod_ax_mul`, `project_y`, `triple_to_axiom`.

**`converter.py`** — `NNFConverter`: converts z3 formulas to negation normal form.

**`visitors.py`** — Visitor base class `SimpleVisit` (memoized traversal) and concrete visitors: `SimpleSimplify` (constant folding + structural simplification), `SimplePropagate` (equality propagation inside `And`/`Or`), `MakeDefs` (introduces fresh constants for nonlinear subterms in products), `HasUninterpreted`, `Contains`.

**`utils.py`** — Arithmetic helpers (`mk_and`, `mk_or`, `mk_mul`, `eval_mul`, `eval_exp`, etc.), `GetLevel`, and small z3 predicates (`is_numeral`, `is_symbolic_const`, `is_ite`, etc.).

**`stats.py`** — `STATS` global singleton tracking iterations, pures created, axioms added, LIA calls, and timing phases.

**`tagged_logging.py`** — Verbosity-gated logging. Each module has a `LOG_TAG` and calls `tagged_logging.mk_logfn(LOG_TAG)`. Verbosity per tag is set in `VERBOSITY_LEVELS`. All modules share the same level set by `-v N`:

| Level | Meaning |
|-------|---------|
| `0`   | Default — quiet (only final result and stats) |
| `1`   | Solver skeleton: iteration count |
| `2`   | Per-iteration results: model, NIA ok/fail, notable heuristic outcomes |
| `3`   | Formulas, assignments, constraints |
| `4`   | Raw z3 internals: raw models, assertion lists, per-pure checks |
| `5`   | Everything: bounds lists, individual atoms, projection steps |

## Key Data Structures

- **Prefix**: `list[QLev]` — quantifier levels (typically one existential level for QF problems, possibly more after `MakeDefs`)
- **Assignment**: `dict[ExprRef, ExprRef]` — maps constants to their current values
- **Pures**: term↔constant bijection; "pure" constants abstract nonlinear subterms so the abstraction is in LIA

## Model Checking

**`check_model.py`** (root-level) — Verifies SAT answers by running the solver with `--print-model`, injecting the returned model as `assert` constraints, and checking with a reference solver (default: z3):

```bash
./check_model.py examples/jain_5-2.c_0.smt2
./check_model.py examples/jain_5-2.c_0.smt2 -- --modax 4
./check_model.py --ref-solver cvc5 examples/jain_5-2.c_0.smt2 -- --bounds
```

## Examples and Testing Inputs

- `examples/` — SMT2 benchmark files used for manual testing
- `testing/fuzzing/` — Fuzzing scripts (`nia_fuzzer.py`, `lia_fuzzer.py`, comparison scripts)
- `data/` — Additional benchmark data

## Linting

```bash
ruff check src/
ruff format src/
```

Line length is 88 (configured in `pyproject.toml`).

## Workflow

- There are no unit tests. Verify fixes by running examples manually or with the fuzzing scripts in `testing/fuzzing/`.
