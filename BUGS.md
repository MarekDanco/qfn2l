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
