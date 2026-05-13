# C++ Code Review - 2026-05-13

## Findings

### Medium: command-line parsing rejects explicit stdin even though usage advertises it

File: `main.cpp:117`

The usage text says `[file.smt2|-]`, and `filename` defaults to `"-"`, but
passing `-` explicitly is rejected because the parser treats any leading dash as
an option unless it matches a known option.

Reproducer:

```sh
printf '(set-logic QF_LIA)\n(declare-const x Int)\n(assert (> x 0))\n(check-sat)\n' \
  | build/qfn2l -
```

Observed:

```text
Unknown option: -
```

Recommended fix: handle `arg == "-"` before the unknown-option branch.

### Medium: unknown or unavailable backend names silently fall back to Z3

File: `main.cpp:129`

With `BACKEND_Z3` enabled, `create_solver` ignores any backend string not
handled by the `BACKEND_CVC5` block and returns a Z3 solver.

Reproducer:

```sh
build/qfn2l --backend definitely-not-a-backend /tmp/qfn2l-review-lia.smt2
```

Observed: the command runs and returns `sat`.

Expected: reject unknown or unavailable backends with a clear error. This is
especially important because users can request `--backend cvc5` and get Z3
without noticing if CVC5 was not compiled in.

### Low: `term_mod_int` documentation is stale

File: `utils.h:48`

The comment says the helper uses Z3 simplification, but the implementation in
`utils.cpp:271` parses the numeral into `cpp_int` and computes the modulus
directly. The implementation is preferable, but the comment can mislead future
maintenance.

### Low: signal handler performs non-async-signal-safe work

File: `main.cpp:32`

The signal handler calls code that performs complex C++ work and stdio output
before `_Exit`. This is often acceptable for command-line tooling, but it is not
async-signal-safe and can deadlock or corrupt state if the signal arrives while
the process is inside allocator/stdio code.

Recommended mitigation: set an atomic shutdown flag in the signal handler and
poll it from the solve loop, or use a timer thread for timeout handling.

## Notes

This review assumes the smt-switch Z3 term-comparison issue is fixed upstream.
The standalone identity repro remains useful as an upstream regression test, but
it is not counted here as a qfn2l codebase issue.

The default test target passes after the review comments:

```text
cmake --build build --target check
```

The standalone smt-switch identity repro remains intentionally excluded from
`check` and still fails with:

```text
9 vs -9: terms compare equal
```
