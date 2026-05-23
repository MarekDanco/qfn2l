# Observations / Ideas for Later

## Targeted pure refinement in `check_nia`

**File:** `src/qfn2l/lia_abstraction.py`, `check_nia()` and `is_okay()`

### Current behaviour

After a SAT result from the LIA solver, `check_nia` iterates over all collected pures and
calls `is_okay(pure, t, model)`, which checks whether `model.eval(pure) == model.eval(t)`.
Any pure where these disagree gets axioms added for it.

### The problem

`p != x*y` in the model does **not** imply that any constraint is actually violated under
true NIA semantics. Consider a constraint `C(p, x, y)`. The LIA model satisfies it (by
construction — it's a model). When we substitute the true NIA value of `x*y` back in,
`C(x*y, x, y)` may still hold, even though `p != x*y`. In that case, refining `p` is
unnecessary work.

The current code refines every pure with a value mismatch, even those whose mismatch
doesn't falsify any constraint. This is over-aggressive.

### The idea

Only refine pures that appear in constraints that are **falsified** under NIA semantics.
Concretely:

1. Traverse `current_pure_body` constraint by constraint (e.g. the conjuncts of the top-level `And`).
2. For each constraint, evaluate it by substituting each pure with the actual NIA value of
   its term (using the model for the linear variables).
3. Collect only the constraints that evaluate to **false** (not just `None`/unknown).
4. Refine only the pures that appear in those falsified constraints.

The purified body still has the original constraint structure — pures just stand in for the
nonlinear subterms — so the traversal is straightforward.

### What happens to `CheckVal`?

The per-constraint approach **subsumes** `CheckVal` as a whole-formula check:

- If zero constraints are falsified → no refinement needed. This is exactly the "quick ok"
  case that `CheckVal` currently catches.
- If some constraints are falsified → refine only the pures in those.

So `CheckVal` as a separate fast path becomes redundant: the per-constraint evaluation
naturally handles both cases in one pass.

There are three options:

**A) Keep `CheckVal` as a cheap fast path, replace `is_okay` with per-constraint evaluation.**
The two-phase structure stays but `CheckVal` only handles the "all ok" shortcut and the
heavier fallback becomes constraint-level rather than pure-level.

**B) Drop `CheckVal` entirely, do per-constraint evaluation directly.**
Simpler — one mechanism instead of two. The "all ok" case falls out for free (empty
falsified set). The cost is losing the formula-level short-circuit, but per-constraint
evaluation should be cheap enough to absorb that.

**C) Modify `CheckVal` to return the set of falsified constraints instead of a boolean.**
Unifies both mechanisms into one traversal. `check()` returning True = "quick ok" as
before. On failure, instead of returning a bare False, it returns the falsified
sub-formulas directly, which are then used to select pures for refinement. This reuses
the three-valued machinery already in `CheckVal` (the `None`/unknown propagation) without
duplication.

Option C is probably the cleanest. The three-valued semantics of `CheckVal` are still
useful here: a constraint evaluating to `None` (undecidable, e.g. uninterpreted sub-terms
or div-by-zero) should not be treated as falsified — only definite `False` warrants
refinement.

### Notes

- `current_pure_body` is already in purified form; the per-constraint evaluation needs to
  substitute pures with their NIA values on the fly — exactly what `CheckVal.visit_purified`
  already does.
- Congruence axioms (`_add_lazy_congruence_axioms`) are a separate mechanism and
  unaffected by this change.
