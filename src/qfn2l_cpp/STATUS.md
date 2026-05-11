# C++ Solver Status

## Build

Two build directories:

```bash
# Release (use for correctness testing)
mkdir -p src/qfn2l_cpp/build_noasan && cd src/qfn2l_cpp/build_noasan
cmake .. -DBACKEND_Z3=ON -DCMAKE_BUILD_TYPE=Release \
         -DSMT_SWITCH_DIR=/home/marek/solvers/smt-switch/build
make -j$(nproc)

# ASAN/debug (use for memory debugging) — pre-configured
cd src/qfn2l_cpp/build && make -j$(nproc)
```

smt-switch is at `/home/marek/solvers/smt-switch/`. See `NOTES.md` for full build instructions.

## Test Results

Run `examples/test_me_cpp.sh` from the `examples/` directory (uses `build_noasan`):

| File | Expected | `--zeros` | `--bounds` | `--zeros --bounds --modax 5` |
|------|----------|-----------|------------|------------------------------|
| `hard.c_2.smt2` | unsat | ✓ | ✓ | ✓ |
| `hard.c_3.smt2` | unsat | ✓ | ✓ | ✓ |
| `hard-ll.c_0..4.smt2` | unsat | ✓ | ✓ | ✓ |
| `STC_0019.smt2` | sat | ✓ | ✓ | ✗ timeout |
| `STC_0072.smt2` | sat | ✗ timeout | ✓ | ✗ timeout |

**Note:** The `STC_0019 --zeros` row was timeout before recent work and is now ✓ (88 iterations).
The test script must be run from the `examples/` directory — `bash examples/test_me_cpp.sh`
run from the repo root uses a wrong relative path for the binary.

Python solver finds `sat` on the timeout cases within 10s — there is still a SAT-solving
effectiveness gap on these instances (C++ takes ~88 iterations vs Python's 6).

## Known Bugs / Open Issues

### 1. ASAN build crashes on several inputs

The `build/` (ASAN) binary exits with code 1 (memory error) on `hard-ll.c_*` and `STC_*` files.
The `build_noasan/` binary handles them correctly. The memory bug has not been diagnosed yet.

To investigate:
```bash
./src/qfn2l_cpp/build/qfn2l examples/hard-ll.c_0.smt2 --timeout 10 2>&1 | grep -A5 "ERROR\|SUMMARY"
```

### 2. SAT iteration gap vs Python (partially addressed)

**Root cause:** smt-switch represents `(* x x x)` from SMT2 as binary `(* x (* x x))`.
The bottom-up `Purifier` creates a pure for the inner `(* x x)` subterm before it can
see the outer context, producing 6 pures (e.g. e_x4 for x², e_x5 for x³) instead of
Python's 3 (only e_x5 for x³).

**Fixes applied (this session):**
- `Purifier::visit_mul`: flattens binary-nested muls by looking through pures-for-muls,
  so `(* x (* x x))` produces one canonical pure `e_x5` for x³.
- `make_pure_constant` smul axioms: use `split_mul` leaf variables instead of raw
  children, so zero-ness axioms are LIA-only (no NIA subterms).
- `set_level`: now calls `CollectPures` on `_current_pure_body` and only includes
  axioms for reachable pures. Intermediate pures (e_x4, e_y2, e_z0) are excluded
  from the LIA — their smul axioms were adding free LIA variables that slowed
  z3's search dramatically.

**Result:** STC_0019 with `--zeros` drops from 300–470 iterations to ~88 (seed-dependent).

**Remaining gap:** C++ still takes ~88 iterations vs Python's 6. Two factors:
1. The intermediate pures (e_x4, e_y2, e_z0) still exist as free unconstrained
   variables in the quantifier prefix — they add no constraints but are present.
   A cleaner fix would prevent them from being created at all.
2. Python z3 (n-ary `(* x x x)`) and C++ smt-switch (binary `(* x (* x x))`) may
   cause z3's internal solver to choose different initial model values, leading to
   different search paths.

**To compare:**
```bash
# Python (6 iterations)
python3 src/qfn2l/qf_solver.py -v3 --timeout 10 --zeros --brief-stats examples/STC_0019.smt2

# C++ (88 iterations)
src/qfn2l_cpp/build_noasan/qfn2l -v 3 --timeout 10 --zeros --brief-stats examples/STC_0019.smt2
```

### 3. Intermediate pures not eliminated from prefix

Even though `set_level` now excludes their axioms, intermediate pures (e.g. e_x4 for x²
when the body only has x³) are still created by `make_pure_constant` and added to the
quantifier prefix. The right fix is to not call `make_pure_constant` for inner mul terms
that will be subsumed by an outer term during purification.

The challenge: `Purifier` is bottom-up, so when processing `(* x x)` we don't yet know
it's part of `(* x x x)`. Options:
- Two-pass purification (collect all mul terms, then only create pures for maximal ones)
- Post-purification cleanup: remove pures not reachable from `CollectPures` on the body

## Suggested Next Steps

1. Fix the ASAN crash (captures a real memory error)
2. Prevent intermediate pures from being created (two-pass purification or post-cleanup)
3. Investigate remaining iteration gap on STC_0019/STC_0072 — check if Python adds
   different/fewer initial axioms or uses different z3 options
4. Audit the `// TODO verify` smt-switch API calls in `NOTES.md`
