# C++ vs Python: known gaps and divergences

Status as of 2026-05-14.  Update this file when gaps are closed.

---

## Missing features

### Preprocessing (`-p`, `-pa`, `-pat`) — not implemented

Python runs z3 tactics before the main loop:
- `-p` / `--preprocess`: `simplify → propagate-values → solve-eqs → simplify`
- `-pa N` / `--preprocess-aggressive`: adds `propagate-ineqs`, `normalize-bounds`,
  `ctx-simplify`, optionally `ctx-solver-simplify`

C++ parses these flags but ignores them.  They can significantly shrink hard
instances before solving starts.

Fix: inside `#ifdef BACKEND_Z3`, obtain the raw `z3::context` and run
`z3::tactic` pipelines before constructing `QfSolver`.

---

## Bug

### Non-z3 fallback (`_solve` cvc5 path) accumulates assertions

The z3 path in `_solve()` uses a fresh local `z3::solver` each call, so it
stays clean.  The non-z3 fallback (below `#endif`) calls
`_ctx.solver->assert_formula(...)` without a surrounding `push()`/`pop()`.
Each iteration of the solving loop adds another copy of
`_current_instantiation`, causing incorrect results with the cvc5 backend.

Fix: add `_ctx.solver->push()` before and `_ctx.solver->pop(1)` after the
non-z3 assert/check block, or reset the solver each call the same way z3 does.

---

## Solver strategy difference — **FIXED 2026-05-13**

### `SolverFor("LIA")` + bounded all-variable re-check

**Was:** C++ used a plain `z3::solver` with `set_logic("QF_NIA")` plus a
bounded pre-check that constrained only `_orig_vars`, leaving pure constants
unconstrained.  z3 4.14's LIA solver lacks the small-model preference of z3
4.17, causing models to grow large over iterations and slowing convergence
dramatically (188 iterations vs Python's 6 for STC_0019).

**Fix applied:**
1. Main per-call solver changed to `z3::solver(*z3ctx, "LIA")` — mirrors
   Python's `SolverFor("LIA")`.
2. Removed bounded pre-check (bounding only orig vars was counterproductive).
3. Added an adaptive post-SAT shrinking loop over **all** model constants
   (orig vars + pures): each attempt bounds every int constant to
   `±(3/4 * current_max)` and re-checks, up to 5 times, stopping when all
   values are below 20 or the bounded check is UNSAT.  Mirrors Python's
   `--bounds` heuristic but applied unconditionally.

**Result** (2026-05-13):

| Benchmark | Python iters | C++ iters before | C++ iters after |
|-----------|-------------|-----------------|-----------------|
| STC_0019 | 6 | 390 (timeout) | 35 (sat, ~0.4 s) |
| STC_0072 | 6 | 114 (sat) | 36 (sat, ~0.4 s) |
| STC_0504 | 110 (unknown) | — | 36 (sat, C++ wins) |

The remaining iteration-count gap (35 vs 6) is due to z3 4.14 vs 4.17 model
quality and does not affect wall-clock time on these benchmarks.

To close the gap entirely: rebuild smt-switch against z3 4.17 (requires
a CMake build of z3 so that `find_package(Z3)` works, then pass
`-DZ3_INSTALL_DIR=/path/to/z3-4.17/install` when configuring smt-switch).

---

## Behavioral divergences

### `_congruence_pairs_added` key stability

Python keys on `(min(a.get_id(), b.get_id()), max(a.get_id(), b.get_id()))`.
z3's `get_id()` is a stable structural integer — same term always gets the
same key across all runs.

C++ (after 2026-05-12 fix) keys on sorted `shared_ptr` pointer addresses.
These are non-deterministic across runs.  The solver may behave slightly
differently across invocations on the same input.

Fix: expose a stable integer ID from smt-switch terms (e.g. via `hash()` if
it is structural, or tag terms at creation time) and key on that instead.

### `set_level` axiom set

Python's `set_level` adds every axiom in `self.axioms.values()`:
```python
mk_and(self.current_pure_body,
       *[substitute(ax, subs) for ls in self.axioms.values() for ax in ls])
```

C++ filters to only axioms for pures reachable from the current body.  The
C++ behaviour is intentionally better (avoids free LIA variables from
unreachable intermediate pures), but it means the two versions assert a
different constraint set, so iteration counts can differ.

### `SimplePropagate` propagation scope

Python only propagates equalities where **both** sides are `is_const`
(atom-level constants).  C++ propagates when one side is a symbolic constant
and the other can be any term.  Slightly more aggressive in C++; could produce
a different simplified formula for certain inputs.

---

## C++ improvements over Python (for reference)

| Feature | Python | C++ |
|---------|--------|-----|
| Adaptive model shrinking (all vars, ×3/4 per round until < 20) | `--bounds` option only | Yes — always on, compensates for z3 4.14 small-model gap |
| Visitor traversal | Recursive (needs `--recursion-depth`) | Iterative post-order |
