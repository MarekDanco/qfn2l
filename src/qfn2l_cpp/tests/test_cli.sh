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

qf_dump="$TMP_DIR/aprove.qfnia.smt2"
abs_dump="$TMP_DIR/aprove.abs.smt2"
"$QFN2L" --timeout 10 \
    --dump-qf-nia "$qf_dump" \
    --dump-abstraction "$abs_dump" \
    "$DATA_DIR/aproveSMT100500964594886141.smt2" \
    >"$TMP_DIR/aprove.out" 2>"$TMP_DIR/aprove.err"

if [[ "$(tr -d '\r\n' <"$TMP_DIR/aprove.out")" != "unknown" ]]; then
    echo "aprove dump run: expected unknown, got:" >&2
    cat "$TMP_DIR/aprove.out" >&2
    exit 1
fi

if [[ ! -s "$qf_dump" || ! -s "$abs_dump" ]]; then
    echo "dump files were not created" >&2
    ls -l "$TMP_DIR" >&2
    exit 1
fi

qf_size="$(wc -c <"$qf_dump")"
abs_size="$(wc -c <"$abs_dump")"
if (( abs_size <= qf_size )); then
    echo "expected abstraction dump to be larger than QF_NIA dump" >&2
    echo "qf=$qf_size abs=$abs_size" >&2
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
