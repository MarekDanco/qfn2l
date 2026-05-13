# C++ Solver Status

## Build

One configured build directory (`build/`, ASAN+debug):

```bash
cd src/qfn2l_cpp/build && make -j$(nproc)
```

smt-switch is at `/home/marek/solvers/smt-switch/`. See `NOTES.md` for full build instructions.

## Test Results

Run `examples/test_me_cpp.sh` from the `examples/` directory:

| File | Expected | `--zeros` | `--bounds` | `--zeros --bounds --modax 5` |
|------|----------|-----------|------------|------------------------------|
| `hard.c_2.smt2` | unsat | ✓ | ✓ | ✓ |
| `hard.c_3.smt2` | unsat | ✓ | ✓ | ✓ |
| `hard-ll.c_0..4.smt2` | unsat | ✓ | ✓ | ✓ |
| `STC_0019.smt2` | sat | ✓ | ✓ | ✓ |
| `STC_0072.smt2` | sat | ✓ | ✓ | ✓ |
| `STC_0504.smt2` | sat | ✓ | ✓ | ✓ |

**Note:** The test script must be run from the `examples/` directory —
`bash examples/test_me_cpp.sh` run from the repo root uses a wrong relative path for the binary.

## Known Bugs / Open Issues

### 1. Intermediate pures not eliminated from prefix

Even though `set_level` now excludes their axioms and the constructor prunes them from
`_axioms`, intermediate pures (e.g. e_x4 for x² when the body only has x³) are still
created by `make_pure_constant` and added to the quantifier prefix as free variables.
The right fix is to not call `make_pure_constant` for inner mul terms that will be
subsumed by an outer term during purification.

The challenge: `Purifier` is bottom-up, so when processing `(* x x)` we don't yet know
it's part of `(* x x x)`. Options:
- Two-pass purification (collect all mul terms, then only create pures for maximal ones)
- Post-purification cleanup: remove pures not reachable from `CollectPures` on the body

## Suggested Next Steps

1. Prevent intermediate pures from being created (two-pass purification or post-cleanup)
2. Audit the `// TODO verify` smt-switch API calls in `NOTES.md`
