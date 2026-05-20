#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 QFN2L_BINARY SOURCE_DIR" >&2
    exit 2
fi

QFN2L="$1"
SOURCE_DIR="$2"
DATA_DIR="$SOURCE_DIR/tests/data"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

run_case() {
    local name="$1"
    local expected="$2"
    local input="$DATA_DIR/$name"
    local stdout="$TMP_DIR/$name.out"
    local stderr="$TMP_DIR/$name.err"

    "$QFN2L" --timeout 10 "$input" >"$stdout" 2>"$stderr"
    if [[ "$(tr -d '\r\n' <"$stdout")" != "$expected" ]]; then
        echo "$name: expected stdout '$expected', got:" >&2
        cat "$stdout" >&2
        exit 1
    fi
    if ! grep -Eq '^backend: z3 [0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$' "$stderr"; then
        echo "$name: missing backend version on stderr:" >&2
        cat "$stderr" >&2
        exit 1
    fi
}

run_case derive_u_eq_vx_unsat.smt2 unsat
run_case derive_ms03_center_diagonal_unsat.smt2 unsat

run_preprocessed_case() {
    local label="$1"
    shift
    local name="derive_c_eq_x2_unsat.smt2"
    local stdout="$TMP_DIR/$name.$label.out"
    local stderr="$TMP_DIR/$name.$label.err"

    "$QFN2L" "$@" --timeout 10 "$DATA_DIR/$name" >"$stdout" 2>"$stderr"
    if [[ "$(tr -d '\r\n' <"$stdout")" != "unsat" ]]; then
        echo "$name ($label): expected stdout 'unsat', got:" >&2
        cat "$stdout" >&2
        exit 1
    fi
}

run_preprocessed_case p -p
run_preprocessed_case pa2 -pa 2

qf_dump="$TMP_DIR/derive_u_eq_vx.qfnia.smt2"
abs_dump="$TMP_DIR/derive_u_eq_vx.abs.smt2"
"$QFN2L" --timeout 10 \
    --dump-qf-nia "$qf_dump" \
    --dump-abstraction "$abs_dump" \
    "$DATA_DIR/derive_u_eq_vx_unsat.smt2" \
    >"$TMP_DIR/dump.out" 2>"$TMP_DIR/dump.err"

if [[ "$(tr -d '\r\n' <"$TMP_DIR/dump.out")" != "unsat" ]]; then
    echo "dump run: expected unsat, got:" >&2
    cat "$TMP_DIR/dump.out" >&2
    exit 1
fi

if [[ ! -s "$qf_dump" || ! -s "$abs_dump" ]]; then
    echo "dump files were not created" >&2
    ls -l "$TMP_DIR" >&2
    exit 1
fi

if ! grep -q '(* v x)' "$qf_dump"; then
    echo "QF_NIA dump does not contain the original nonlinear product" >&2
    cat "$qf_dump" >&2
    exit 1
fi

if ! grep -q 'e_vx_' "$abs_dump"; then
    echo "abstraction dump does not contain the expected product abstraction" >&2
    cat "$abs_dump" >&2
    exit 1
fi

if "$QFN2L" --dump-solve-formula "$TMP_DIR/old.dump" \
    "$DATA_DIR/derive_c_eq_x2_unsat.smt2" \
    >"$TMP_DIR/old.out" 2>"$TMP_DIR/old.err"; then
    echo "old dump option unexpectedly succeeded" >&2
    exit 1
fi

if ! grep -q 'Unknown option: --dump-solve-formula' "$TMP_DIR/old.err"; then
    echo "old dump option did not report the expected error" >&2
    cat "$TMP_DIR/old.err" >&2
    exit 1
fi
