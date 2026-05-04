# Code Review: qfn2l QF_NIA Solver

*Date: 2026-05-04*

---

## Correctness Issues / Potential Bugs

### [MEDIUM] `eval_exp` latent crash on `exp=0` — `utils.py:142`

`eval_exp(x, 0)` calls `eval_mul([])` (list-wrapped, not star-unpacked), which eventually calls `z3.Product()` returning a Python `int(1)`, causing `Z3Exception` on `simplify`. Not triggered by current call sites (all have `exp >= 1`) but the `assert exp >= 0` contract misleadingly permits it.

Fix: change to `assert exp >= 1` or star-unpack:
```python
eval_mul(*(exp * [x]))
```

### [MEDIUM] `incorporate_assumptions` can raise `KeyError` — `lia_abstraction.py:436–439`

```python
assumptions.remove(p)  # crashes if p came from `bounds`, not `assumptions`
```

The unsat core may contain elements passed via `*bounds` that are not in the `assumptions` set.

Fix: `assumptions.discard(p)`

### [LOW] `_check_correct` NNF validator has wrong cache key — `converter.py:63–73`

`_check_cache` is keyed by `expr` alone, ignoring `seen_not`. The same sub-expression appearing both inside and outside a `Not` will get a stale cached result. Only affects the debug `check=True` path, but makes the validator unreliable.

Fix: use `(expr, seen_not)` as key.

### [LOW] `SimplePropagate` equality chain resolution is order-dependent — `visitors.py:193–213`

`eqs` is a `set` with unspecified iteration order, making chain propagation like `x==y, y==5 → x=5` inconsistently resolved. Not a soundness bug but degrades preprocessing predictability.

Fix: use a `list` to preserve insertion order.

---

## Dead / Unreachable Code

These functions exist but are never called — safe to remove (~100 lines total):

- `collect_symbolic_constants` + `_collect_symbolic_constants_memo` global dict — `utils.py:271–287`
- `var_subs` — `utils.py:291–331`
- `print_smt2_formula` — `utils.py:35–38`
- `InterpretZeroDivision` — `visitors.py:426–450` (the visitor is never instantiated; the `mod_zero_interp`/`idiv_zero_interp` dicts are populated but never consumed)
- `EvalUnderPures`, `CollectAtoms` — `pures.py`

Also: `self.current_pure_body` is initialized twice in `LiaAbstraction.__init__` (lines 164 and 177) — remove the first.

---

## Performance Issues

### `HasUninterpreted` memo grows unboundedly — `lia_abstraction.py`

`self.hu = HasUninterpreted()` is created once and reused. Its inherited `_memo` dict retains every expression ever visited for the solver's lifetime. Same applies to `self.simpl` and `self.prop`. On long runs with many axioms this is a slow memory leak.

### `CollectPures.visit_node` double-traverses children — `pures.py:98–114`

`SimpleVisit` already queues all children before calling `visit_node`. The manual child loop inside `visit_node` is redundant (hits cache but still iterates). Only the axiom walk `self(ax)` is the novel logic.

### `SimpleSimplify` instances created per axiom call — `projections.py:112,137`

`combine_lb_left`/`combine_ub_left` each instantiate a fresh `SimpleSimplify()`. A shared module-level instance would reuse the memo across calls.

---

## Error Handling / Robustness

### Parse errors from z3 are unhandled — `qf_solver.py:272–276`

`s.from_file(...)` raises `Z3Exception` on malformed SMT2 but there is no try/except. The user sees a raw Python traceback.

Fix:
```python
try:
    s.from_file(opts.filename)
except z3.Z3Exception as e:
    print(f"Parse error: {e}", file=sys.stderr)
    sys.exit(1)
```

### `SIGALRM` is POSIX-only

Used by `--timeout` without a platform guard. Non-issue if Windows is out of scope, but worth a comment.

### `check_model.py` does not catch `FileNotFoundError`

When the reference solver binary doesn't exist the exception propagates unhandled.

---

## Suggested Ruff Configuration

Current `pyproject.toml` only enables `E`/`W` rules. Adding these catches more substantive issues:

```toml
select = ["E", "W", "F", "N"]
```

`F401` would flag the unused `mk_not` import in `lia_abstraction.py`.

---

## Priority Summary

| Priority | Issue | File |
|---|---|---|
| Fix | `assumptions.remove` → `.discard` | `lia_abstraction.py:438` |
| Fix | `eval_exp` assert + unpack | `utils.py:142` |
| Fix | `_check_correct` cache key | `converter.py:63` |
| Cleanup | Remove ~100 lines of dead code | multiple |
| Cleanup | Duplicate `current_pure_body` init | `lia_abstraction.py:164` |
| Improve | Parse error handling in `main` | `qf_solver.py` |
| Improve | Document `assignment` vs `current_model` split in `check_nia` | `lia_abstraction.py` |

The two medium-priority bugs (`discard` and `eval_exp`) are the most actionable fixes — the rest are cleanup and robustness improvements.
