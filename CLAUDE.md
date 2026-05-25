# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

qfn2l is a quantifier-free NIA (Nonlinear Integer Arithmetic) solver. It solves QF_NIA SMT2 formulas over integer arithmetic including multiplication, integer division (`div`), and modulo (`mod`).

The solver's approach: abstract nonlinear operations into fresh constants ("pures"), solve the resulting LIA (Linear Integer Arithmetic) abstraction, then check if the NIA semantics are satisfied, adding axioms on failures.

**Development policy: all solver logic changes go in the C++ implementation (`src/qfn2l_cpp/`). The Python implementation (`src/qfn2l/`) is reference only.**

## C++ Solver

### Building

```bash
cd src/qfn2l_cpp/build && make -j$(nproc)
```

smt-switch is at `/home/marek/solvers/smt-switch/`. See `src/qfn2l_cpp/NOTES.md` for full first-time build instructions (CMake flags, smt-switch setup).

### Running

```bash
qfn2l-cpp examples/hard.c_2.smt2
qfn2l-cpp --timeout 30 --bounds examples/STC_0019.smt2
```

The `qfn2l-cpp` shell command resolves to `src/qfn2l_cpp/build/qfn2l`. Most flags match the Python solver (see below), with these differences:

- `--preproc-aggressive N` / `-pa N` / `--preproc` / `-p` — implemented (calls z3 tactics via smt-switch)
- `--no-congruence` — disable lazy congruence axioms
- `--lia-preproc` — preprocess LIA formula with z3 simplify+propagate before each solve
- `--tangent` — tangent plane axioms for x\*y products
- `--frontier` — frontier strategy for tangent lemmas (requires `--tangent`)
- `--model-fix` / `--model-fix2` — model repair heuristics for mul factors
- `--backend NAME` — solver backend: `z3` (default) or `cvc5`
- `--recursion-depth` is absent (C++ uses iterative traversal)

### Testing

Unit tests (CTest):
```bash
cd src/qfn2l_cpp/build && make check
```

Integration tests (must run from `examples/`):
```bash
cd examples && bash test_me_cpp.sh
```

Known open issue: intermediate pures (e.g. `e_x4` for `x²` when the formula has `x³`) are added to the prefix but not needed; see `src/qfn2l_cpp/STATUS.md`.

## Python Solver (reference)

## Running the Solver

The main entry point is `src/qfn2l/qf_solver.py`. SMT2 files are provided as input:

```bash
python3 src/qfn2l/qf_solver.py examples/hard.c_2.smt2
python3 src/qfn2l/qf_solver.py -v3 examples/hard.c_2.smt2   # verbosity level 3
python3 src/qfn2l/qf_solver.py -p examples/hard.c_2.smt2    # preprocess with z3 tactics
python3 src/qfn2l/qf_solver.py -pa 1 examples/hard.c_2.smt2 # aggressive preprocessing level 1
python3 src/qfn2l/qf_solver.py --modax 4 examples/hard.c_2.smt2  # modulo axioms up to value 4
```

Key options:
- `-v N` — verbosity (default 0; higher = more output; module tags: `[slv]`, `[abs]`)
- `--maxits N` — limit iterations (returns `unknown`)
- `--timeout N` — wall-clock timeout in seconds (float, -1 = no limit); reliable because it sets z3's internal C-level timeout on each LIA call
- `--bounds` / `--zeros` — heuristics for LIA solver
- `--static` — add static axioms for `div`/`mod`
- `--seed N` — set z3 random seed (default 7)
- `--modax N` — modulo axioms up to value N (default 2; <=1 disables)
- `-p` / `--preprocess` — preprocess with z3 tactics (simplify + propagate-values + solve-eqs)
- `-pa N` / `--preprocess-aggressive N` — aggressive preprocessing level (1 or 2)
- `-pat N` / `--preprocess-aggressive-timeout N` — timeout for aggressive preprocessing in ms (default 5000)
- `--heur-timeout N` — timeout for heuristic LIA calls in ms (default 3000)
- `--print-model` — print SAT model as SMT2 define-fun lines
- `--stats` — print full stats on exit (default: silent)
- `--brief-stats` — on exit print only: terminated phase, longest phase, iteration count, pures count
- `--recursion-depth N` — set Python recursion limit (default 100000)

Note: shell `timeout` / SIGTERM is **unreliable** for this solver — z3 can block in C code and never deliver the signal to Python. Use `--timeout` instead. SIGTERM and SIGINT are handled gracefully (prints stats before exit).

## Architecture

All source is in `src/qfn2l/`. The main modules:

**`qf_solver.py`** — Entry point (`main()`). The `QfSolver` class drives the solving loop:
1. Convert to NNF → propagate equalities → introduce definitions via `MakeDefs`
2. Build a single-level existential prefix from the free variables
3. Repeat: call `LiaAbstraction.solve()`, check NIA correctness via `check_nia()`, add axioms on failure

Also contains `z3_preprocess()` and `z3_preprocess_aggressive()` for optional formula simplification before solving.

**`lia_abstraction.py`** — `LiaAbstraction` class. Core logic for:
- *Purification* (`Purifier` inner class): replaces nonlinear subterms (mul, div, mod with uninterpreted args) with fresh constants called "pures". Static axioms (`--static`) are added during purification for div/mod.
- `set_level()`: instantiates the abstraction under the current variable assignment
- `solve()`: calls z3 LIA solver, with optional `--bounds`/`--zeros` heuristics via `incorporate_assumptions()`
- `check_nia()`: first does a fast three-valued check via `CheckVal`; on failure, collects active pures via `CollectPures`, adds lazy congruence axioms if enabled, then generates targeted axioms per failing pure
- Axiom generation: `mk_mul_axioms`, `mk_mod_axiom`, `mk_idiv_axiom`, `mk_pow_axioms`, `mk_mixed_mul_axioms`
- Lazy congruence: `_add_lazy_congruence_axioms` (pairwise, only adds axioms violated by the current model)
- `LIAFail` exception: raised when the LIA solver returns `unknown` (e.g. timeout), propagates up to return `None` from `QfSolver.solve()`

**`prefix.py`** — `QLev` represents one quantifier level (forall or exists, plus a list of variables). Also provides `to_fla()` to reconstruct a quantified z3 formula from a prefix + body.

**`pures.py`** — `Pures` bidirectional map between NIA terms and their pure constants. `CollectPures` traverses a formula collecting active pures (split by type: `mul_collected`, `idiv_collected`, `mod_collected`). `CheckVal` is a three-valued NIA checker — evaluates pures against actual NIA values; returns `True` if all ok, `False` if any mismatch, `None` if undecidable (uninterpreted sub-terms).

**`projections.py`** — Linear arithmetic bounds for NIA terms (used in axiom generation): `lin_lb_pow`, `lin_ub_pow`, `combine_lb`, `combine_ub`, `mod_ax_mul`, `project_y`, `triple_to_axiom`.

**`converter.py`** — `NNFConverter`: converts z3 formulas to negation normal form.

**`visitors.py`** — Visitor base class `SimpleVisit` (memoized traversal) and concrete visitors: `SimpleSimplify` (constant folding + structural simplification), `SimplePropagate` (equality propagation inside `And`/`Or`), `MakeDefs` (introduces fresh constants for nonlinear subterms in products, extending the prefix), `HasUninterpreted`, `Contains`.

**`utils.py`** — Arithmetic helpers (`mk_and`, `mk_or`, `mk_mul`, `eval_mul`, `eval_exp`, `split_mul`, etc.) and small z3 predicates (`is_numeral`, `is_symbolic_const`, `is_ite`, `is_non_linear`, etc.).

**`stats.py`** — `STATS` global singleton (`Stats` class) tracking iterations, pures created, axioms added (mul/mod/div separately), LIA calls, and timing phases. `timed_check()` wraps z3 solver calls to accumulate `liatime`. `brief_prn()` prints a compact summary.

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
- **Axioms**: `defaultdict[ExprRef, list[BoolRef]]` — per-pure list of axioms added so far; included in every `set_level()` instantiation

## Model Checking

**`check_model.py`** (root-level) — Verifies SAT answers by running the solver with `--print-model`, injecting the returned model as `assert` constraints, and checking with a reference solver (default: z3):

```bash
./check_model.py examples/hard.c_2.smt2
./check_model.py examples/hard.c_2.smt2 -- --modax 4
./check_model.py --ref-solver cvc5 examples/hard.c_2.smt2 -- --bounds
```

## Examples and Testing Inputs

- `examples/` — SMT2 benchmark files used for manual testing
- `testing/fuzzing/` — Fuzzing scripts: `nia_fuzzer.py`, `lia_fuzzer.py`, `nia_gen.py`, `compare.sh`, `flatten.py`, and shell runners (`run_nia_fuzzer.sh`, `run_lia_fuzzer.sh`, `run_qfnia_fuzzer.sh`)
- `data/` — Additional benchmark data
- `logs/` — Solver run logs

## Linting

```bash
ruff check src/
ruff format src/
```

Line length is 88 (configured in `pyproject.toml`).

## Workflow

- C++ has CTest unit tests (`cd src/qfn2l_cpp/build && make check`) and integration tests (`cd examples && bash test_me_cpp.sh`). Python has no unit tests — verify by running examples manually or with fuzzing scripts in `testing/fuzzing/`.
- `make_table.py` / `table.txt` — benchmark result table generation (root-level, untracked).
- `src/qfn2l_cpp/CPP_VS_PYTHON.md` — documents behavioral divergences between the two implementations.
