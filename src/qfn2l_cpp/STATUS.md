# C++ Solver Status

## Build

Two build directories (both already CMake-configured):

```bash
# Release (use for correctness testing)
cd src/qfn2l_cpp/build_noasan && make -j$(nproc)

# ASAN/debug (use for memory debugging)
cd src/qfn2l_cpp/build && make -j$(nproc)
```

smt-switch is at `/home/marek/solvers/smt-switch/`. See `NOTES.md` for full build instructions.

## Test Results

Run `examples/test_me_cpp.sh` (uses `build_noasan`):

| File | Expected | `--zeros` | `--bounds` | `--zeros --bounds --modax 5` |
|------|----------|-----------|------------|------------------------------|
| `hard.c_2.smt2` | unsat | ✓ | ✓ | ✓ |
| `hard.c_3.smt2` | unsat | ✓ | ✓ | ✓ |
| `hard-ll.c_0..4.smt2` | unsat | ✓ | ✓ | ✓ |
| `STC_0019.smt2` | sat | ✓ | ✓ | ✗ timeout |
| `STC_0072.smt2` | sat | ✗ timeout | ✓ | ✗ timeout |

Python solver finds `sat` on the timeout cases within 10s — there is a SAT-solving effectiveness gap on these instances.

## Known Bugs

### 1. ASAN build crashes on several inputs

The `build/` (ASAN) binary exits with code 1 (memory error) on `hard-ll.c_*` and `STC_*` files. The `build_noasan/` binary handles them correctly. The memory bug has not been diagnosed yet.

To investigate:
```bash
./src/qfn2l_cpp/build/qfn2l examples/hard-ll.c_0.smt2 --timeout 10 2>&1 | grep -A5 "ERROR\|SUMMARY"
```

### 2. SAT timeout gap vs Python

`STC_0019` and `STC_0072` time out in C++ under `--zeros` but Python finds `sat` within 10s. Likely a heuristic or search-order difference. To compare:

```bash
# Python
python3 src/qfn2l/qf_solver.py -v3 --timeout 10 --zeros examples/STC_0072.smt2

# C++
src/qfn2l_cpp/build_noasan/qfn2l -v3 --timeout 10 --zeros examples/STC_0072.smt2
```

## Suggested Next Steps

1. Fix the ASAN crash (captures a real memory error — fix it before it causes silent corruption in release builds)
2. Investigate and close the SAT timeout gap on STC instances
3. Audit the `// TODO verify` smt-switch API calls listed in `NOTES.md` if unexpected behavior appears
