# Bug investigation: --model-fix produces invalid models [FIXED 2026-05-26]

**Symptoms**: Running with `--bounds --tangent --no-congruence --model-fix` on the
`bugs/Stroeder_15__GulwaniJainKoskinen-PLDI2009-Fig1_true-termination.c__p2XXXX_edge_closing_0.smt2`
files produces models that fail `check_model_cpp.py --ref-solver cvc5` (cvc5 returns
UNSAT when substituting the printed model values into the original formula).

---

## What was found

### Symptom in iteration 13

The final iteration (13) logs:

```
[abs] model_fix: implicant=123 wrong_pures=0 relevant_vars=0
[abs] check_nia ok (all implicant literals NIA-satisfied)
```

`wrong_pures=0` means the implicant-based check found no pures whose NIA value
(actual product) differed from their LIA value (model value). So `check_nia` returns
true and the (bad) model is printed.

---

## Fix already applied: `visit_complex` in `pures.cpp`

**Root cause of `wrong_pures=0`**: The `visit_complex` function in `pures.cpp`
was falling back to `try_get_value(_lia_solver, t)` for nonlinear terms (like
`(* x y)`) after computing all child values. The LIA solver's `get_value` cannot
evaluate nonlinear arithmetic and returns `nullopt`. When `tv = nullopt`, `pure_is_wrong`
returns `false` (conservative), so all pures look "not wrong" even if they actually
are.

**Fix** (already committed to `pures.cpp`): In `visit_complex`, after all children
have concrete integer values, compute the result directly instead of asking the LIA
solver:

```cpp
// mul: r = product of all children
// idiv/mod: Euclidean semantics (remainder always non-negative)
```

This means the zero shortcut is now extended: even when no factor is zero, the
actual product/div/mod is computed. This fixes cases where the zero shortcut was
the only path that worked before.

**BUT the bug persists even with this fix.** The iteration 13 behavior is
identical: `wrong_pures=0, relevant_vars=0`. So there is a second issue.

---

## Second issue (not yet fixed): LIA formula vs. original formula

### What the LIA formula looks like

Running with `--dump-abstraction /tmp/lia_formula.smt2` reveals that the LIA
formula contains:

```
(or a!16 (= UndefCntr2_undef6 0))
```

where `a!16` expands to (roughly):

```
(and
  (or (and (= UndefCntr0_CT UndefCntr2_CT)
           (= UndefCntr0_undef6 UndefCntr1_undef6)   ← missing: (= UndefCntr0_undef6 UndefCntr2_undef6)
           (or a!13 a!14 a!15))
      a!15)
  (= UndefCntr2_main_id 0)
  (= UndefCntr2_main_maxId 0)
  (= UndefCntr2_main_tmp 0))
```

The ORIGINAL formula's corresponding assertion requires
`(= UndefCntr0_undef6 UndefCntr2_undef6)` in the equality branch, but the LIA
formula only has `(= UndefCntr0_undef6 UndefCntr1_undef6)`. The LIA formula
is **weaker** than the original.

### Which constraints fail

With the printed model values:
- `UndefCntr0_undef6 = 3`, `UndefCntr1_undef6 = 3`, `UndefCntr2_undef6 = -1`
- `UndefCntr0_CT = 4`, `UndefCntr1_CT = -4`, `UndefCntr2_CT = 4`

**Assertions [14] and [16]** (0-indexed, from the original formula) are UNSAT.
Specifically assertion [16] says:

```
(or (= UndefCntr2_undef6 0)          ← -1 = 0? FALSE
    (and ...
         (or
           (and (= (+ (- 1) FV_undef6_2) 0) ...)  ← FV_undef6_2=2: 1=0? FALSE
           (and (= UndefCntr0_undef6 UndefCntr2_undef6) ...)  ← 3=-1? FALSE
         )))
```

Both disjuncts fail. But the LIA formula only requires
`(= UndefCntr0_undef6 UndefCntr1_undef6)` (= 3=3, TRUE), which is why the LIA
formula is SAT.

### Where the weakening comes from

The LIA formula is the `--dump-abstraction` output **after 13 iterations**, so it
includes all accumulated axioms. The weakening likely originates from how z3's
`let`-binding / simplification represents the formula, or from how `SimplePropagate`
or `MakeDefs` handled the equality chains.

Need to check: is the original purified formula (before any axioms) already weaker?
Compare `--dump-qf-nia FILE` (post-preprocessing NIA formula) vs what the LIA solver
actually gets.

---

## Leads / next steps

1. **Check if the LIA formula (at iteration 0, no axioms yet) is already weaker**
   than the original formula. Run:
   ```
   qfn2l-cpp --dump-abstraction /tmp/it0.smt2 --maxits 0 ... file.smt2
   ```
   and verify that the NIA formula corresponds to the LIA formula correctly.

2. **Alternative hypothesis — repopulation assigns wrong values to unpinned variables**:
   In `_solve()`, the LIA model is replicated to `_ctx.solver` by pinning only the
   constants in `mdl.num_consts()`. Variables that are "don't care" in the LIA model
   (z3 didn't need to assign them a specific value) are NOT pinned. After `slv.check()`
   on the repopulated solver, z3 assigns these variables arbitrarily. Both `check_nia()`
   and `print_model()` then use these arbitrary values, which may not satisfy the
   original NIA formula even if all pures are NIA-correct.

   Fix candidate: pin ALL variables from `_orig_vars` in the repopulated solver,
   extracting their values from the LIA model via `mdl.eval(var, /*completion=*/true)`.

3. **The `visit_complex` fix is correct** and should stay. It prevents a separate
   class of false-positive `check_nia` passes (when products have non-zero factors
   that the LIA solver can't evaluate). Even if it doesn't fix this specific bug,
   it's needed for correctness.

---

## Files changed so far

- `src/qfn2l_cpp/pures.cpp`: `visit_complex` now computes mul/idiv/mod directly
  from concrete child values instead of falling through to `try_get_value(_lia_solver, t)`.

---

## How to reproduce

```bash
python3 testing/check_model_cpp.py --ref-solver cvc5 \
  bugs/Stroeder_15__GulwaniJainKoskinen-PLDI2009-Fig1_true-termination.c__p23277_edge_closing_0.smt2 \
  -- --bounds --tangent --no-congruence --model-fix
```

Expected: FAIL (was failing before fix; now returns unknown — no longer produces an invalid model).

Verbose investigation:
```bash
qfn2l-cpp --bounds --tangent --no-congruence --model-fix -v 4 \
  bugs/Stroeder_15__GulwaniJainKoskinen-PLDI2009-Fig1_true-termination.c__p23277_edge_closing_0.smt2 \
  2>&1 | awk '/\[slv\] it: 13/,0'
```
