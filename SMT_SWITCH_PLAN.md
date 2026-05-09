# Migration Plan: Python → C++ with smt-switch

## Goal

Rewrite the solver in C++ using [smt-switch](https://github.com/stanford-centaur/smt-switch) as the SMT API. This gives native performance, true backend-agnosticism (z3, CVC5, Bitwuzla), and removes the Python/z3 dependency entirely.

---

## C++ Project Structure

```
src/
  main.cpp                  # argument parsing, entry point
  qf_solver.{h,cpp}         # QfSolver class — main solving loop
  lia_abstraction.{h,cpp}   # LiaAbstraction + Purifier
  visitors.{h,cpp}          # term traversal, simplification, MakeDefs
  pures.{h,cpp}             # Pures bidirectional map, CheckVal
  projections.{h,cpp}       # axiom bound generation
  converter.{h,cpp}         # NNF conversion
  prefix.{h,cpp}            # QLev, quantifier prefix
  level_info.{h,cpp}        # FormulaInfo, GetLevel
  utils.{h,cpp}             # mk_and/or/mul, type predicates, arithmetic helpers
  stats.{h,cpp}             # STATS singleton
  tagged_logging.{h,cpp}    # verbosity-gated logging
CMakeLists.txt
```

---

## Key smt-switch C++ Types

| Concept | C++ type |
|---------|----------|
| Solver | `smt::SmtSolver` (= `shared_ptr<AbsSmtSolver>`) |
| Term | `smt::Term` (= `shared_ptr<AbsTerm>`) |
| Sort | `smt::Sort` (= `shared_ptr<AbsSort>`) |
| Operator | `smt::Op` (e.g. `smt::And`, `smt::Plus`, `smt::Mult`) |
| Result | `smt::Result` |
| Term vector | `smt::TermVec` (= `vector<Term>`) |
| Term→Term map | `smt::UnorderedTermMap` (hash map with `TermHash`) |
| Term set | `smt::UnorderedTermSet` |

smt-switch provides `TermHash` and `TermEqual` so Terms can be used in `unordered_map`/`unordered_set` directly.

---

## API Mapping

### Solver lifecycle

| Python/z3 | C++ smt-switch |
|-----------|----------------|
| `z3.SolverFor("LIA")` | `smt::SmtSolver s = smt::Cvc5SolverFactory::create(false); s->set_logic("LIA");` |
| `solver.add(f)` | `solver->assert_formula(f)` |
| `solver.push()` / `pop()` | `solver->push()` / `solver->pop()` |
| `solver.check()` | `smt::Result r = solver->check_sat()` |
| `r == z3.sat` | `r.is_sat()` |
| `r == z3.unsat` | `r.is_unsat()` |
| `r == z3.unknown` | `r.is_unknown()` |
| `model.eval(t)` | `solver->get_value(t)` |
| `solver.unsat_core()` | `smt::TermVec core; solver->get_unsat_core(core)` |
| `solver.set(timeout=N)` | `solver->set_opt("timeout", to_string(N))` |
| `z3.set_param("smt.random_seed", N)` | `solver->set_opt("seed", to_string(N))` |

### Sorts

| Python/z3 | C++ smt-switch |
|-----------|----------------|
| `z3.IntSort()` | `solver->make_sort(smt::INT)` |
| `z3.BoolSort()` | `solver->make_sort(smt::BOOL)` |
| `e.sort()` | `term->get_sort()` |
| `z3.is_int(e)` | `term->get_sort() == int_sort` |
| `z3.is_bool(e)` | `term->get_sort() == bool_sort` |

### Term construction

| Python/z3 | C++ smt-switch |
|-----------|----------------|
| `z3.Int("x")` | `solver->make_symbol("x", int_sort)` |
| `z3.FreshConst(sort)` | `solver->make_symbol(fresh_name(), sort)` |
| `z3.IntVal(n)` | `solver->make_term(n, int_sort)` |
| `z3.BoolVal(True)` | `solver->make_term(true)` |
| `z3.And(a, b)` | `solver->make_term(smt::And, a, b)` |
| `z3.Or(a, b)` | `solver->make_term(smt::Or, a, b)` |
| `z3.Not(a)` | `solver->make_term(smt::Not, a)` |
| `z3.Implies(a, b)` | `solver->make_term(smt::Implies, a, b)` |
| `a + b` | `solver->make_term(smt::Plus, a, b)` |
| `a - b` | `solver->make_term(smt::Minus, a, b)` |
| `a * b` | `solver->make_term(smt::Mult, a, b)` |
| `z3.If(c, a, b)` | `solver->make_term(smt::Ite, c, a, b)` |
| `a / b` (int div) | `solver->make_term(smt::IntDiv, a, b)` |
| `a % b` | `solver->make_term(smt::Mod, a, b)` |
| `a <= b` | `solver->make_term(smt::Le, a, b)` |
| `a == b` | `solver->make_term(smt::Equal, a, b)` |
| `z3.Abs(a)` | `solver->make_term(smt::Ite, solver->make_term(smt::Ge, a, zero), a, solver->make_term(smt::Neg, a))` |

### AST introspection

| Python/z3 | C++ smt-switch |
|-----------|----------------|
| `is_app(e)` | `term->get_kind() == smt::APPLICATION` |
| `is_const(e)` | `term->get_kind() == smt::SYMBOL` |
| `is_numeral(e)` / `e.as_long()` | `term->is_value()` / `term->to_int()` |
| `is_mul(e)` | `term->get_op() == smt::Op(smt::Mult)` |
| `is_mod(e)` | `term->get_op() == smt::Op(smt::Mod)` |
| `is_idiv(e)` | `term->get_op() == smt::Op(smt::IntDiv)` |
| `is_and(e)` | `term->get_op() == smt::Op(smt::And)` |
| `is_or(e)` | `term->get_op() == smt::Op(smt::Or)` |
| `is_not(e)` | `term->get_op() == smt::Op(smt::Not)` |
| `is_eq(e)` | `term->get_op() == smt::Op(smt::Equal)` |
| `e.num_args()` | `term->get_num_indices()` or `distance(term->begin(), term->end())` |
| `e.arg(i)` | via iterator: `auto it = term->begin(); advance(it, i); *it` |
| `e.children()` | `TermVec(term->begin(), term->end())` |
| `e.decl()(new_args)` | `solver->make_term(term->get_op(), new_args)` |

### Substitution

| Python/z3 | C++ smt-switch |
|-----------|----------------|
| `z3.substitute(e, [(old,new),...])` | `solver->substitute(e, UnorderedTermMap{{old, new}, ...})` |
| `z3.simplify(e)` | `solver->simplify(e)` (backend-dependent) |

### Quantifiers

smt-switch has limited quantifier support. The solver is effectively quantifier-free at solve time (quantifiers are eliminated before the LIA loop), so this is manageable:

| Python/z3 | C++ smt-switch |
|-----------|----------------|
| `z3.ForAll(vars, body)` | `solver->make_term(smt::Forall, ...)` — verify support per backend |
| `z3.Exists(vars, body)` | `solver->make_term(smt::Exists, ...)` |
| `z3.substitute_vars(body, vars)` | Manual: collect bound vars during quantifier traversal; use `solver->substitute` |

### SMT2 parsing

| Python/z3 | C++ smt-switch |
|-----------|----------------|
| `solver.from_file(path)` | `solver->from_smtlib_file(path)` (verify exact name) |
| `solver.from_string(s)` | `solver->from_smtlib_string(s)` |
| `solver.assertions()` | `solver->get_assertions(assertions_vec)` |

---

## Build System (CMakeLists.txt)

```cmake
cmake_minimum_required(VERSION 3.16)
project(qfn2l CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(smt-switch REQUIRED)   # or add_subdirectory if vendored
find_package(smt-switch-z3 REQUIRED)
find_package(smt-switch-cvc5 REQUIRED)

add_executable(qfn2l
  src/main.cpp
  src/qf_solver.cpp
  src/lia_abstraction.cpp
  src/visitors.cpp
  src/pures.cpp
  src/projections.cpp
  src/converter.cpp
  src/prefix.cpp
  src/level_info.cpp
  src/utils.cpp
  src/stats.cpp
  src/tagged_logging.cpp
)
target_link_libraries(qfn2l PRIVATE smt-switch smt-switch-z3 smt-switch-cvc5)
```

Use [CLI11](https://github.com/CLIUtils/CLI11) (header-only) for argument parsing — mirrors Python's argparse closely.

---

## Migration Phases

### Phase 0: Build skeleton

- Set up `CMakeLists.txt`, fetch smt-switch (as a git submodule or via FetchContent), CLI11.
- Write `main.cpp` that just prints "hello" and links against smt-switch.
- Confirm at least one backend (z3) builds and a trivial `check_sat` works.
- Verify: `term->get_op()`, child iteration, `term->to_int()`, `solver->substitute()`.

### Phase 1: `tagged_logging.h/.cpp` and `stats.h/.cpp`

These have no smt-switch dependency. Port them first as they're needed everywhere:
- `tagged_logging`: verbosity int + tag filter → `fprintf`/`printf` macros.
- `STATS`: plain struct with counters and `std::chrono` for timing.

### Phase 2: `utils.h/.cpp`

Port the arithmetic helpers and type predicates. This is the foundation for all later phases:
- `mk_and`, `mk_or`, `mk_not`, `mk_mul`, `mk_add` with short-circuit constant elimination.
- `is_numeral`, `is_symbolic_const`, `is_ite`, `is_linear_mul`, etc. — reimplement using `get_kind()`/`get_op()`.
- `eval_mul`, `eval_exp` — arithmetic on `int64_t` values extracted from numeral terms.
- `fresh_name()` — thread-local counter producing `"_fresh_N"` strings.
- `get_vars(term)` — recursive walk collecting `SYMBOL`-kind terms into an `UnorderedTermSet`.

### Phase 3: `visitors.h/.cpp`

The Python visitor pattern maps to a recursive function template in C++:

```cpp
// Memoized term transformer
using TermTransformer = std::function<Term(const Term&)>;
Term transform(SmtSolver& s, const Term& t,
               TermTransformer fn,
               UnorderedTermMap& cache);
```

Port each visitor as a function that builds its own cache and calls `transform`:
- `simple_simplify(s, t)` — constant folding + structural reduction.
- `simple_propagate(s, t, equalities)` — equality propagation inside And/Or.
- `make_defs(s, t, defs_out)` — introduces fresh constants for nonlinear subterms in products; returns a `TermVec` of new definitions.
- `has_uninterpreted(s, t)` → `bool`.
- `contains(s, t, sub)` → `bool`.

### Phase 4: `converter.h/.cpp`

NNF conversion. The solver is QF so quantifiers appear only during parsing and are immediately eliminated:
- Traverse the parsed term; flip negations inward.
- For quantifiers: extract bound variables (using `term->get_op()`, children), replace de-Bruijn indices with fresh symbols via `solver->substitute`.
- Chain comparison breaking, boolean equality rewriting — same logic as Python, mechanically ported.

### Phase 5: `prefix.h/.cpp` and `level_info.h/.cpp`

- `QLev` struct: `{bool is_forall; TermVec vars}`.
- `to_formula(SmtSolver&, prefix, body)` — wraps body in `Forall`/`Exists` terms.
- `FormulaInfo` struct: stores prefix + body; `GetLevel` is a recursive walk.

### Phase 6: `pures.h/.cpp`

- `Pures` class: two `UnorderedTermMap`s (term→pure, pure→term).
- `CollectPures`: recursive walk populating a set of active pures.
- `CheckVal`: evaluate a term given a model (`solver->get_value`), handling three-valued logic for partial assignments via `std::optional<int64_t>`.
- Model completion (replacing `model.update_value`): `Pures` or `LiaAbstraction` holds a `UnorderedTermMap completion_` that shadows `solver->get_value` for manually assigned constants.

### Phase 7: `projections.h/.cpp`

Arithmetic axiom generation. Purely computational — port line-by-line using the `utils` helpers from Phase 2. No special C++ considerations.

### Phase 8: `lia_abstraction.h/.cpp`

The core class. This is the largest file and benefits most from the earlier phases being solid:
- Constructor takes `SmtSolver`, `FormulaInfo`, verbosity config.
- `Purifier` inner class (or nested struct): port directly.
- `set_level(assignment)`: apply assignment via `solver->substitute`.
- `solve()`: `solver->push()`, `solver->assert_formula()`, `solver->check_sat()`, result handling, `solver->pop()`.
- All axiom methods (`mk_mul_axioms`, `mk_mod_axiom`, etc.): port using Phase 2–7 helpers.
- UNSAT core: `solver->get_unsat_core(core)`.
- Timeout: `solver->set_opt("timeout", ms_str)` before each `check_sat`.

### Phase 9: `qf_solver.h/.cpp` and `main.cpp`

- `main.cpp`: CLI11 for argument parsing (mirrors Python argparse flags exactly).
- SMT2 parsing: `solver->from_smtlib_file(path)` + `solver->get_assertions(vec)`.
- Tactic preprocessing (`-p`/`-pa`): **drop or defer** — z3's tactic API is not exposed by smt-switch. Options:
  - Shell out to `z3 --tactic ... input.smt2 > preprocessed.smt2` and re-parse.
  - Drop for the initial C++ version; add back if needed.
- `QfSolver::solve()`: the main loop — port directly, all helpers already exist.
- Model printing: iterate known symbols, call `solver->get_value`.
- `--print-model` flag: emit `(define-fun ...)` lines to stdout.

---

## Open Questions / Risks

1. **`term->to_int()` API name**: Verify the exact method for extracting an integer value from a numeral term. May be `to_int()`, `to_string()` + parse, or backend-specific.

2. **`solver->simplify()`**: Availability varies by backend. If absent, implement a pure C++ constant-folding pass in `utils.cpp` as fallback — this is sufficient for the load-bearing cases (evaluating `5 * 3 → 15`).

3. **Quantifier support in smt-switch**: smt-switch targets QF logics primarily. Verify that `Forall`/`Exists` terms can be constructed and traversed, even if backends don't reason about them (they're only needed transiently during NNF conversion before elimination).

4. **`solver->from_smtlib_file` parsing**: After parsing, verify that the resulting terms use the same smt-switch term representation so the rest of the pipeline works without a conversion step.

5. **UNSAT core with CVC5**: Requires `solver->set_opt("produce-unsat-cores", "true")` before any assertions. Confirm smt-switch exposes this.

6. **Tactic preprocessing**: z3's `Then`/`With`/`TryFor` tactic API has no smt-switch equivalent. The cleanest approach for `-p`/`-pa` is to shell out to z3 as a preprocessor and re-parse the result.

7. **`check_model.py`**: This root-level script verifies SAT answers. It will need either a C++ port or a wrapper that calls the new binary and then a reference solver. Low priority.

---

## File Change Summary

| File | Action |
|------|--------|
| `src/tagged_logging.{h,cpp}` | New (Phase 1) |
| `src/stats.{h,cpp}` | New (Phase 1) |
| `src/utils.{h,cpp}` | New (Phase 2) |
| `src/visitors.{h,cpp}` | New (Phase 3) |
| `src/converter.{h,cpp}` | New (Phase 4) |
| `src/prefix.{h,cpp}` | New (Phase 5) |
| `src/level_info.{h,cpp}` | New (Phase 5) |
| `src/pures.{h,cpp}` | New (Phase 6) |
| `src/projections.{h,cpp}` | New (Phase 7) |
| `src/lia_abstraction.{h,cpp}` | New (Phase 8) |
| `src/qf_solver.{h,cpp}` | New (Phase 9) |
| `src/main.cpp` | New (Phase 9) |
| `CMakeLists.txt` | New (Phase 0) |
| `src/qfn2l/` (Python) | Kept as reference; delete after verification |

## Suggested order

Phase 0 → 1 → 2 → 3 → 7 → 6 → 5 → 4 → 8 → 9

After each phase, test with: `./qfn2l examples/jain_5-2.c_0.smt2 --timeout 30`
