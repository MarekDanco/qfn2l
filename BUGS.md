# Known Bugs / Bug History

## [FIXED] SimplePropagate drops duplicate-lhs equalities — visitors.cpp

**Introduced:** initial C++ port
**Fixed:** 2026-05-26
**Severity:** Soundness — LIA abstraction is weaker than the original NIA formula; solver can produce models that fail the original constraints
**Trigger:** Any formula where the same variable appears as lhs in two equalities within the same `and` block, e.g. `(and (= x a) (= x b) ...)`. All `bugs/Stroeder_15__GulwaniJainKoskinen-PLDI2009-Fig1_true-termination.c__p2XXXX_edge_closing_0.smt2` files triggered this.

### Root cause

`SimplePropagate::propagate` extracts equalities of the form `(= lhs rhs)` from an `and` block, removes them from the child list, and builds a substitution map `subst[lhs] = rhs`. When two equalities share the same `lhs` (e.g., `x=y` and `x=z`), both are removed from the child list but `subst[lhs]` is overwritten — only the last value survives. The rebuild step emits one equality (`x=y` or `x=z`), silently dropping the other.

In the triggering formulas, `(and (= UndefCntr0_undef6 UndefCntr1_undef6) (= UndefCntr0_undef6 UndefCntr2_undef6) ...)` had the second equality dropped. The LIA abstraction became logically weaker, allowing models where `UndefCntr1_undef6 ≠ UndefCntr2_undef6` — invalid under the original formula.

### Fix

Track which lhs variables have already been extracted (`extracted_lhs` set). If a second equality has a lhs already in the set, skip extracting it (leave it in `chs`). The substitution then rewrites it in-place: `x=z` with `subst[x]=y` becomes `y=z`, preserving the constraint.

```cpp
smt::UnorderedTermSet extracted_lhs;
// ...
if (extracted_lhs.count(lhs)) continue;
extracted_lhs.insert(lhs);
eqs.push_back({lhs, rhs});
chs[i] = chs.back(); chs.pop_back();
```

---

## [FIXED] Bounds heuristic skipped when zeros heuristic fails — lia_abstraction.cpp

**Introduced:** initial C++ port
**Fixed:** 2026-05-11
**Severity:** Effectiveness — `--zeros --bounds` combination behaves like `--zeros` alone; `--zeros --bounds --modax 5` times out on STC_0019 and STC_0072
**Trigger:** Any problem where `--zeros` fails (all zero assumptions are UNSAT, e.g. `x³+y³+z³=19` where no pure can be 0) and `--bounds` is also enabled.

### Root cause

When the zeros heuristic fails (every `check_sat_assuming({pure==0})` is UNSAT and all
assumptions are removed by the unsat-core loop), the solver is left in UNSAT state.
`apply_bounds_heuristic` is then called immediately. It tries to read `get_value(p)` for each
pure to compute the maximum pure value, but since the solver is in UNSAT state these calls
throw and the exception is silently caught. `mx` stays 0 < 20, and the bounds heuristic returns
without doing anything.

In Python, `current_model` is an independent z3 model object that persists through heuristic
failures — bounds always has valid pure values to shrink. In C++, model values come from live
`get_value()` queries on the solver, which requires the solver to be in a SAT state.

### Fix

Restore the solver to SAT state (call `check_sat()`) between the zeros and bounds heuristics
when the zeros heuristic left the solver in UNSAT state:

```cpp
if (_heuristic_left_unsat) {
    _ctx.solver->check_sat();  // restore valid model for bounds
    _heuristic_left_unsat = false;
}
if (_opts.bounds) apply_bounds_heuristic(cur_pures, zero_assumptions);
```

---

## [FIXED] mod_ax_mul: `true_mod` not reduced mod `k` — projections.py:151

**Introduced:** unknown
**Fixed:** 2026-03-11 (commit pending)
**Severity:** Soundness — can produce incorrect UNSAT for satisfiable formulas
**Trigger:** `--modax 3` (or higher), when both multiplicands are ≡ 2 (mod 3) in the LIA model
**Reproducer:** `nia-solver --modax 3 examples/modax_bug.smt2` (expected SAT, was UNSAT)

### Root cause

`mod_ax_mul` generates the axiom:

> *if x ≡ a (mod k) and y ≡ b (mod k), then x·y ≡ (a·b) (mod k)*

The code accumulated the product of per-factor residues in a loop, but never reduced the final product mod `k`:

```python
true_mod = 1
for _, exp, v in pows:
    true_mod *= ((v % k) ** exp) % k   # each factor ∈ [0, k-1]
# true_mod can now be up to (k-1)² — never reduced!
```

With k=3 and two factors both ≡ 2 (mod 3): `true_mod = 2 × 2 = 4`.
The generated axiom was `Implies(x%3==2 ∧ y%3==2, pure%3==4)`.
Since `pure % 3 ∈ {0, 1, 2}`, the conclusion is unsatisfiable, making the
implication equivalent to `¬(x%3==2 ∧ y%3==2)` — blocking all solutions
where both roots are ≡ 2 (mod 3).

### Fix

Add `true_mod %= k` after the accumulation loop (projections.py:151).

### Pattern to watch for

Whenever computing `(a₁ · a₂ · … · aₙ) mod k` in a loop, you must reduce
mod k after *each multiplication* (or at minimum after the full product).
Computing `(a mod k)` for each factor individually is not sufficient — the
product of reduced residues can still exceed k.

---

## [FIXED] Signed integer overflow in `eval_exp` / `eval_mul` — utils.cpp:272,242

**Introduced:** unknown
**Fixed:** 2026-05-10
**Severity:** Soundness — wrong NIA evaluations on large values; caused SAT timeout loop on STC_0019
**Trigger:** Any formula where a pure's NIA value requires multiplying two large integers (e.g. 3054444803811²)

### Root cause

`eval_exp` and `eval_mul` tried to compute the result as `int64_t` and fall back to symbolic computation on overflow, but the fallback relied on `std::out_of_range` being thrown:

```cpp
int64_t v = term_to_int64(x), r = 1;
for (int i = 0; i < n; ++i) r = r * v;  // silent UB on overflow
```

`term_to_int64` throws `std::out_of_range` for values that don't fit in uint64, but `int64_t * int64_t` overflow is undefined behavior in C++ — it does not throw. So the catch block was never reached and the wrapped garbage value was used as the NIA evaluation result.

### Fix

Use `__builtin_mul_overflow` to detect overflow and throw explicitly:

```cpp
for (int i = 0; i < n; ++i)
    if (__builtin_mul_overflow(r, v, &r))
        throw std::out_of_range("overflow");
```

Same fix applied to `eval_mul`.

---

## [FIXED] `--bounds` heuristic returns UNSAT for SAT formula — lia_abstraction.cpp

**Introduced:** unknown (was masked by the overflow bug above)
**Fixed:** 2026-05-11
**Severity:** Soundness — `--bounds` can return incorrect UNSAT
**Trigger:** `STC_0019.smt2` (and possibly others with large pure values)

```
build_noasan/qfn2l --bounds examples/STC_0019.smt2  →  unsat  (WRONG, pre-fix)
build_noasan/qfn2l --bounds examples/STC_0019.smt2  →  sat    (correct, post-fix)
```

### Root cause

In `apply_bounds_heuristic`, when `check_sat_assuming(bounds)` returned non-SAT
(bounds too tight), the function returned early without setting
`_heuristic_left_unsat = true`. This left the solver in UNSAT state.
The recovery `check_sat()` in `_solve()` (which only fires when
`_heuristic_left_unsat` is true) was never run. Subsequent `get_value()`
calls in `solve()` on the UNSAT solver threw exceptions, were silently caught,
and caused `solve()` to return `nullopt` — making the caller treat a SAT
instance as UNSAT.

In Python, `current_model` is preserved through bounds failure (it's only
updated on bounds *success*), so the old model is always available.
In C++, the model is queried live from the solver, so the solver state must
be restored after a failed `check_sat_assuming`.

### Fix

One line in `lia_abstraction.cpp` — set the flag before returning:

```cpp
if (!res.is_sat()) { _heuristic_left_unsat = true; return; }
```

---

## [NOT A BUG] `# continue` commented out after mod/idiv in `add_bt_axioms` — lia_abstraction.py:327,329

**Verdict:** Intentional. The `# continue` was deliberately commented out.
The misleading comment `# assert False, "Dead code"` below it is stale and should be removed.

### Explanation

After `mk_mod_axiom` / `mk_idiv_axiom`, the code falls through to a generic
substitution-axiom block. This is intentional because the two kinds of axioms
are *complementary*:

- `mk_mod_axiom` conditions on **one** argument at a time:
  `Implies(y==yval, pure == x % yval)` or `Implies(x==xval ∧ |y|>|xval|, pure == ...)`

- The generic block conditions on **both** projected arguments jointly:
  `Implies(x==xval ∧ y==yval, pure == xval % yval)`

When both x and y are projected variables (common in backtracking), the joint
axiom is strictly stronger and was observed to be necessary for the solver to
prove some problems. The `assert proj_cs` is safe because the level check
`to_lev < get_level(t) < from_lev` guarantees at least one child is projected.
