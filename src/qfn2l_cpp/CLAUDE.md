# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This is the C++ implementation of the qfn2l QF_NIA solver. It is the primary development target — all solver logic changes go here, never in the Python reference implementation. The solver abstracts nonlinear operations (mul, div, mod) into fresh LIA constants ("pures"), solves the linear abstraction, and adds axioms on NIA failures.

## Build

smt-switch is at `/home/marek/solvers/smt-switch/` (build: `/home/marek/solvers/smt-switch/build`). The build is already configured in `build/`.

```bash
# Rebuild after changes
cd build && make -j$(nproc)

# First-time configure (uses the ./configure wrapper)
./configure                          # release, default smt-switch path
./configure --debug                  # debug/ASAN build
./configure --smt-switch-dir /path   # custom smt-switch location
```

## Running

```bash
qfn2l-cpp examples/hard.c_2.smt2 --timeout 30
qfn2l-cpp --bounds --zeros examples/STC_0019.smt2 --timeout 60
```

`qfn2l-cpp` resolves to `build/qfn2l`. Always pass `--timeout N` when running interactively — without it the solver can hang indefinitely inside z3's C code.

## Testing

```bash
# Unit tests (run from build/)
cd build && make check

# Run a single test binary directly
./build/qfn2l_projections_tests

# Integration tests (MUST run from examples/)
cd ../../examples && bash test_me_cpp.sh
```

The CLI test (`tests/test_cli.sh`) exercises argument parsing and dump flags; it is invoked by `make check` automatically. Unit test sources are in `tests/`.

## Architecture

The solving loop (one iteration = one LIA call + NIA check + axiom generation) is split across three layers:

**`main.cpp`** — argument parsing, SIGALRM timeout (`setitimer`), Z3 tactic preprocessing (`-p`/`-pa`), model printing with MC chain reconstruction. The Z3 backend is the only one that supports SMT2 parsing (`zctx.parse_file`).

**`qf_solver.cpp` / `qf_solver.h`** — `QfSolver` drives the outer loop: NNF conversion → `SimplePropagate` → `MakeDefs` (introduces fresh existentials for nonlinear sub-terms, extending the prefix) → iterates `LiaAbstraction::solve()` + `LiaAbstraction::check_nia()`.

**`lia_abstraction.cpp` / `lia_abstraction.h`** — the core. Contains:
- `Purifier` (inner class): bottom-up term transformer that replaces nonlinear sub-terms with fresh pure constants. Currently creates intermediate pures for inner products (known issue, see `STATUS.md`).
- `solve()`: calls the z3 LIA solver (`z3::solver(*ctx, "LIA")`), with optional heuristics (`--bounds`, `--zeros`, `--model-fix`).
- `check_nia()`: fast three-valued check via `CheckVal`; on failure calls axiom generators and `add_lazy_congruence_axioms`.
- Axiom generators: `mk_mul_axioms`, `mk_mod_axiom`, `mk_idiv_axiom`, `mk_tangent_at`, `mk_sign_axioms`, `mk_pow_axioms`, `mk_mixed_mul_axioms`.

**`visitors.h` / `visitors.cpp`** — base classes `TermTransformer` (memoized bottom-up rewriting) and `TermPredicate` (memoized predicate). Concrete classes: `SimpleSimplify`, `SimplePropagate`, `FlattenMul`, `MakeDefs`, `HasUninterpreted`, `Contains`. All traversals are iterative (no Python recursion-depth concern).

**`pures.h` / `pures.cpp`** — `Pures` (bidirectional term↔pure map), `CollectPures` (traverses the current body collecting reachable pures by kind: mul/div/mod), `CheckVal` (three-valued NIA evaluation against the current LIA model).

**`utils.h` / `utils.cpp`** — `Ctx` struct (wraps `smt::SmtSolver` + cached sorts/constants), all term-building helpers (`mk_and`, `mk_mul`, `mk_pow`, etc.), `term_to_cpp_int`/`cpp_int_to_term` (uses `boost::multiprecision::cpp_int` for large integers), predicate helpers (`is_mul`, `is_idiv`, etc.).

**`projections.cpp`** — linear arithmetic bound projections used in axiom generation.

**`prefix.h` / `prefix.cpp`** — `QLev` (one quantifier level) and `Prefix` (list of `QLev`).

**`stats.h` / `stats.cpp`** — `STATS` global singleton with phase timers; `Stats::brief_prn()` / `Stats::prn()`.

**`tagged_logging.h`** — verbosity-gated logging; global `g_verbosity`.

## Key Design Notes

- The Z3 path in `_solve()` creates a fresh `z3::solver` per call (no push/pop needed). The cvc5 fallback path has a known bug: it accumulates assertions without push/pop (see `CPP_VS_PYTHON.md`).
- `LIAFail` exception propagates z3 `unknown` up through `solve()` to `QfSolver::solve()`, which returns `nullopt`.
- Pures are named with a "fancy name" encoding the NIA sub-term (e.g. `e_vx_` for `v*x`). The prefix accumulates new pures across iterations.
- `set_level` only includes axioms for pures reachable from the current body (unlike Python which includes all axioms). This avoids free LIA variables but means C++ and Python can diverge in iteration count.
- `_congruence_pairs_added` is keyed on `shared_ptr` pointer addresses (non-deterministic across runs). Python keys on stable `z3.get_id()`.

## Known Issues

See `STATUS.md` for the full list. Key open issue: intermediate pures (e.g. `e_x4` for x² when the formula only has x³) are created by `Purifier` and added to the prefix. The fix requires two-pass purification or post-purification cleanup.

## Backend

The z3 backend is compiled by default (`-DBACKEND_Z3=ON`). smt-switch is configured against z3 4.14 (at `/home/marek/solvers/smt-switch/`). Upgrading to z3 4.17 would reduce iteration counts on SAT benchmarks (see `CPP_VS_PYTHON.md`).
