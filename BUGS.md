# Known Bugs / Bug History

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
