# C++ Review Issues - 2026-05-12

This note records the obvious issues found in the C++ sources under this
directory. The nested multiplication flattening issue in `lia_abstraction.cpp`
has been omitted because it was already fixed.

## 1. `FormulaInfo` memoizes levels before variables are registered

Location: `level_info.cpp`, `FormulaInfo::FormulaInfo`

`_get_level(ctx, _const2lev, body)` is constructed before `_const2lev` is
populated from `prefix`. The `GetLevel` constructor immediately traverses the
body and memoizes levels using an empty variable-level map. Later calls to
`get_level()` can therefore return stale or incorrect levels.

Impact: pures can be placed at the wrong quantifier level, and level filtering
in abstraction checks can become unsound.

Suggested fix: populate `_const2lev` before constructing or using `GetLevel`.
If member order makes that awkward, delay traversal until after the constructor
body has registered prefix variables.

## 2. Backend selection accepts CVC5 but parsing requires Z3

Locations: `main.cpp`, `create_solver`; `main.cpp`, `parse_input`

`--backend cvc5` can create a CVC5 solver when compiled in, but `parse_input`
then dynamic-casts the context solver to `Z3Solver` and exits if that fails.
Unknown backend names also silently fall back to Z3 when Z3 is compiled.

Impact: the advertised backend option is misleading, and invalid backend names
are not rejected.

Suggested fix: either restrict parsing/backend support to Z3 explicitly, or
parse with a dedicated Z3 parser and translate terms into the selected backend.
Also reject unknown backend names during argument handling or solver creation.

## 3. Signal handler performs non-async-signal-safe work

Location: `main.cpp`, `handle_signal`

The signal handler calls stats code, chrono, `printf`, `fflush`, and exits from
inside the handler. Those operations are not async-signal-safe.

Impact: SIGINT, SIGTERM, or SIGALRM can deadlock or corrupt process state,
especially while the solver or allocator holds internal locks.

Suggested fix: have the handler set a `volatile sig_atomic_t` flag only. Check
that flag from the main solving loop, or use a timer/thread mechanism that can
perform normal C++ work outside signal context.

## 4. CMake fallback path disagrees with the documented local dependency layout

Location: `CMakeLists.txt`, `SMT_SWITCH_DIR` fallback

The comments describe `deps/smt-switch` under this directory, and this checkout
has a local `deps/`, but the fallback default is:

```cmake
${CMAKE_SOURCE_DIR}/../../deps/smt-switch/build
```

Impact: a fresh configure can fail even when the dependency is placed exactly
where the local documentation says to put it.

Suggested fix: point the fallback at `${CMAKE_SOURCE_DIR}/deps/smt-switch/build`
or update the documentation to match the intended tree layout.
