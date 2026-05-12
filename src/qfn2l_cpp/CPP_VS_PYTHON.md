# C++ vs Python: known gaps and divergences

Status as of 2026-05-12.  Update this file when gaps are closed.

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

## Solver strategy difference (likely the most impactful)

### `SolverFor("LIA")` vs `set_logic("QF_NIA")`

Python:
```python
self.current_solver = SolverFor("LIA")
```
This instantiates z3's LIA-tuned tactic internally.

C++:
```cpp
ctx.solver->set_logic("QF_NIA");
```
`QF_NIA` is a permissive logic that may route z3 to a different (slower)
solver path for linear problems.

Fix: inside `#ifdef BACKEND_Z3`, create the per-call `z3::solver` using the
`lra` or `qflra` tactic: `z3::tactic(*z3ctx, "lra")`.

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
| Bounded LIA pre-check (`[-1000,+1000]`) | No | Yes — tries bounded check first |
| Warm-start hints | No | Yes — `_prev_var_hints` + `set_initial_value` |
| Visitor traversal | Recursive (needs `--recursion-depth`) | Iterative post-order |
