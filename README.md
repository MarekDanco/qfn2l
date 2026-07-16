# qfn2l

An SMT solver for quantifier-free nonlinear integer arithmetic (QF_NIA), based
on incremental linearization: nonlinear subterms (multiplication, `div`, `mod`)
are abstracted by fresh integer constants, the resulting QF_LIA abstraction is
solved by Z3, and linear axioms are added lazily until the abstraction either
becomes unsatisfiable or yields a model that is correct under the nonlinear
semantics.

Accompanies the paper *Revisiting Incremental Linearization for Nonlinear
Integer Arithmetic* (M. Dančo, K. Chvalovský, M. Janota).

## Building

The solver is written in C++ on top of the
[smt-switch](https://github.com/stanford-centaur/smt-switch) abstraction layer,
with Z3 as the default backend.

```bash
cd src/qfn2l_cpp
mkdir build && cd build
cmake .. -DBACKEND_Z3=ON -DCMAKE_BUILD_TYPE=Release \
         -DSMT_SWITCH_DIR=/path/to/smt-switch/build
make -j$(nproc)
```

See `src/qfn2l_cpp/NOTES.md` for detailed build notes.

## Usage

```bash
qfn2l file.smt2 --timeout 30
```

Selected options (see `--help` for the full list):

- `--timeout N` — wall-clock timeout in seconds
- `--tangent` — tangent-plane axioms for products
- `--frontier` — frontier strategy for tangent lemmas (requires `--tangent`)
- `--model-fix` / `--model-fix2` — model-repair heuristics
- `--backend NAME` — solver backend: `z3` (default) or `cvc5`

## Repository layout

- `src/qfn2l_cpp/` — the C++ solver
- `src/qfn2l/` — reference Python implementation
- `examples/` — SMT2 benchmark files (`cd examples && bash test_me_cpp.sh` runs
  the integration tests)
- `testing/fuzzing/` — fuzzing scripts
- `paper/` — LaTeX sources of the paper

## License

MIT — see [LICENSE](LICENSE).
