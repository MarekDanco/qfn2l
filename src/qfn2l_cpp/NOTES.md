# Build notes

## Dependencies

- **smt-switch** with at least one solver backend (z3 or CVC5).
  See CMakeLists.txt for how to point `SMT_SWITCH_DIR` at a local build.

## Building

```bash
cd src/qfn2l_cpp
mkdir build && cd build
cmake .. -DBACKEND_Z3=ON -DCMAKE_BUILD_TYPE=Release \
         -DSMT_SWITCH_DIR=/path/to/smt-switch/build
make -j$(nproc)
./qfn2l ../../examples/jain_5-2.c_0.smt2 --timeout 30
```

## Known TODOs (API verification needed)

These places are marked `// TODO` in the source and need to be verified
against the installed version of smt-switch before the build will succeed:

1. **`utils.cpp:term_to_int64`** — uses `stoll(t->to_string())` as fallback.
   If `AbsTerm::to_int()` exists, use it instead.

2. **`utils.cpp:is_int_value` / `is_symbol`** — `smt::SYMBOL`, `smt::APPLICATION`,
   `smt::CONSTANT` are the expected `TermKind` enum values; verify names.

3. **`utils.cpp:is_idiv`** — uses `smt::Div`; verify this is the correct
   `PrimOp` for SMT-LIB integer `div`.

4. **`lia_abstraction.cpp:make_lia_solver`** — factory header and class name
   depend on which smt-switch backend packages are installed.

5. **`lia_abstraction.cpp:Purifier::visit_mul`** — calls `solver->simplify(c)`
   which may not exist on all backends; replace with `eval_mul` if needed.

6. **`lia_abstraction.cpp:check_sat_assuming`** — used for UNSAT-core
   heuristics; verify API name (may be `check_sat_assuming_list` or similar).

7. **`main.cpp:parse_input`** — calls `solver->from_smtlib_file` /
   `solver->from_smtlib_string` and `solver->get_assertions`; verify names.

8. **`converter.cpp:quantifier handling`** — uses `get_children(quantifier)[:-1]`
   as bound vars and last child as body; verify smt-switch quantifier encoding.

9. **`prefix.cpp:to_fla`** — passes `[v1, v2, ..., body]` to
   `make_term(Forall/Exists, ...)`: verify this calling convention.

## Structural differences from Python

| Python | C++ |
|--------|-----|
| `z3.Model.get_interp` / `model.eval` | `lia_solver->get_value(t)` |
| `model.update_value` (model completion) | Built into `solve()` via `get_value` |
| `SIGALRM` timeout | Same (POSIX only; see main.cpp TODO) |
| `z3.z3util.get_vars` | `get_vars()` in utils.cpp |
| Tactic preprocessing (`-p`/`-pa`) | **Not implemented** — shell out to z3 CLI |
