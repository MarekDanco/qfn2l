# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

n2l is a quantified NIA (Nonlinear Integer Arithmetic) solver. It solves SMT2 formulas with alternating quantifiers (∀/∃) over integer arithmetic including multiplication, integer division (`div`), and modulo (`mod`).

The solver's approach: abstract nonlinear operations into fresh constants ("pures"), solve the resulting LIA (Linear Integer Arithmetic) abstraction, then check if the NIA semantics are satisfied, adding axioms on failures.

## Running the Solver

The solver is installed as `n2l` (symlink to `src/n2l/solver.py`). SMT2 files are provided as input:

```bash
n2l examples/t0.smt2
n2l -v3 examples/b7.smt2          # verbosity level 3
n2l --all-exists examples/b7.smt2  # heuristic: treat all quantifiers as exists
n2l --fproj examples/t0.smt2       # enable fancy (model-based) projections
n2l -p examples/t0.smt2            # preprocess with z3 tactics
n2l -pa 1 examples/t0.smt2         # aggressive preprocessing level 1
n2l --modax 4 examples/t0.smt2     # modulo axioms up to value 4
```

Key options:
- `-v N` — verbosity (default 1; higher = more output; module tags: `[slv]`, `[abs]`, `[mbp]`, `[prx]`)
- `--maxbts N` / `--maxits N` — limit backtracks/iterations (returns `unknown`)
- `--timeout N` — wall-clock timeout in seconds (float, -1 = no limit); reliable because it sets z3's internal C-level timeout on each LIA call
- `--rename-to-readable` — rename variables to x,y,z,... for debugging
- `--bounds` / `--zeros` — heuristics for LIA solver
- `--static` — add static axioms for `div`/`mod`
- `--seed N` — set z3 random seed (default 7)
- `-pat N` / `--preprocess-aggressive-timeout N` — timeout for aggressive preprocessing in ms (default 5000)
- `--heur-timeout N` — timeout for heuristic N/LIA calls in ms (default 3000)

Note: shell `timeout` / SIGTERM is **unreliable** for this solver — z3 can block in C code and never deliver the signal to Python. Use `--timeout` instead.

## Architecture

All source is in `src/n2l/`. The main modules:

**`solver.py`** — Entry point (`main()`). The `Solver` class drives the top-level CDCL-like loop:
1. Convert to NNF → prenex normal form → introduce definitions via `MakeDefs`
2. Alternate between exists (`eabstraction`) and forall (`aabstraction`) levels
3. On each level: build LIA abstraction, call z3 LIA solver, check NIA correctness
4. On NIA failure: add axioms and retry; on LIA unsat: backtrack two levels

**`lia_abstraction.py`** — `LiaAbstraction` class. Core logic for:
- *Purification* (`Purifier` inner class): replaces nonlinear subterms (mul, div, mod with uninterpreted args) with fresh constants called "pures"
- `set_level()`: instantiates the abstraction under the current variable assignment
- `solve()`: calls z3 LIA solver, with optional `--bounds`/`--zeros`/`--all-exists` heuristics
- `check_nia()`: verifies pure constants match their actual NIA values; adds axioms on mismatch
- Axiom generation: `mk_mul_axioms`, `mk_mod_axiom`, `mk_idiv_axiom`, `mk_pow_axioms`, `mk_mixed_mul_axioms`, `mk_congruence_axioms`

**`prefix.py`** / **`prenex.py`** — `QLev` represents one quantifier level (forall or exists, plus a list of variables). `Prenex.to_prenex()` converts NNF formulas to prenex normal form, merging commuting quantifier blocks.

**`level_info.py`** — `FormulaInfo` tracks which quantifier level each constant/term belongs to.

**`pures.py`** — `Pures` bidirectional map between NIA terms and their pure constants. `CollectPures` traverses a formula collecting active pures.

**`projections.py`** — Linear arithmetic bounds for NIA terms (used in axiom generation): `lin_lb_pow`, `lin_ub_pow`, `combine_lb`, `combine_ub`, `mod_ax_mul`.

**`model_proj.py`** — `Proj` class for model-based projection (fancy `--fproj` option): eliminates projected variables using virtual term substitution.

**`poly.py`** — Polynomial representation used by `model_proj.py`.

**`converter.py`** — `NNFConverter`: converts z3 formulas to negation normal form.

**`visitors.py`** — Visitor base class `SimpleVisit` (memoized traversal) and concrete visitors: `SimpleSimplify` (constant folding + structural simplification), `SimplePropagate` (equality propagation inside `And`/`Or`), `MakeDefs` (introduces fresh constants for nonlinear subterms in products), `HasUninterpreted`, `Contains`, `InterpretZeroDivision`.

**`utils.py`** — Arithmetic helpers (`mk_and`, `mk_or`, `mk_mul`, `eval_mul`, `eval_exp`, etc.), `GetLevel`, and small z3 predicates (`is_numeral`, `is_symbolic_const`, `is_ite`, etc.).

**`stats.py`** — `STATS` global singleton tracking iterations, backtracks, LIA calls, LIA time.

**`tagged_logging.py`** — Verbosity-gated logging. Each module has a `LOG_TAG` and calls `tagged_logging.mk_logfn(LOG_TAG)`. Verbosity per tag is set in `VERBOSITY_LEVELS`. All modules share the same level set by `-v N`:

| Level | Meaning |
|-------|---------|
| `-1`  | Always shown — errors and unusual states |
| `1`   | Solver skeleton: iteration count, current level, backtracks |
| `2`   | Per-iteration results: model, NIA ok/fail, notable heuristic outcomes |
| `3`   | Formulas, assignments, constraints |
| `4`   | Raw z3 internals: raw models, assertion lists, per-pure checks |
| `5`   | Everything: bounds lists, individual atoms, projection steps |

**`z3_cache.py`** — Memoization helper for z3 operations.

## Key Data Structures

- **Prefix**: `list[QLev]` — quantifier alternation levels (index = level number)
- **Assignment**: `dict[ExprRef, ExprRef]` — maps constants to their current values
- **Pures**: term↔constant bijection; "pure" constants abstract nonlinear subterms so the abstraction is in LIA
- The `eabstraction` is the existential player's view (negated prefix), `aabstraction` is the universal player's view

## Model Checking

**`check_model.py`** (root-level) — Verifies SAT answers by running the solver with `--print-model`, injecting the returned model as `assert` constraints, and checking with a reference solver (default: z3):

```bash
./check_model.py examples/b1.smt2
./check_model.py examples/b1.smt2 -- --fproj --modax 4
./check_model.py --ref-solver cvc5 examples/b1.smt2 -- --bounds
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
