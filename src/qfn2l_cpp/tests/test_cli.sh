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

# ── --uf / congruence tests ───────────────────────────────────────────────────

# With congruence disabled and a small iteration budget, the solver cannot
# prove UNSAT for a formula that requires only congruence reasoning.
cong_input="$DATA_DIR/congruence_uf.smt2"

cong_out="$TMP_DIR/cong_no_congruence.out"
"$QFN2L" --no-congruence --maxits 5 --timeout 10 "$cong_input" >"$cong_out" 2>/dev/null
if [[ "$(tr -d '\r\n' <"$cong_out")" != "unknown" ]]; then
    echo "congruence_uf: --no-congruence --maxits 5 expected 'unknown', got:" >&2
    cat "$cong_out" >&2
    exit 1
fi

# --uf solves it trivially via EUF congruence.
uf_out="$TMP_DIR/cong_uf.out"
"$QFN2L" --uf --timeout 10 "$cong_input" >"$uf_out" 2>/dev/null
if [[ "$(tr -d '\r\n' <"$uf_out")" != "unsat" ]]; then
    echo "congruence_uf: --uf expected 'unsat', got:" >&2
    cat "$uf_out" >&2
    exit 1
fi

# Combining --uf with --no-congruence must emit a warning on stderr.
warn_err="$TMP_DIR/cong_uf_warn.err"
"$QFN2L" --uf --no-congruence --timeout 10 "$cong_input" >/dev/null 2>"$warn_err"
if ! grep -q 'no-congruence.*no effect.*--uf\|--uf.*no-congruence' "$warn_err"; then
    echo "congruence_uf: --uf --no-congruence expected warning on stderr, got:" >&2
    cat "$warn_err" >&2
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

# ── model correctness tests ───────────────────────────────────────────────────
# Run the solver with --print-model, extract the model, inject it as assertions
# into the original formula, and confirm z3 reports sat.

check_model_valid() {
    local label="$1"
    local input="$2"
    local out="$TMP_DIR/model_${label}.out"
    shift 2

    "$QFN2L" --timeout 10 --print-model "$@" "$input" >"$out" 2>/dev/null

    local verdict
    verdict="$(grep -E '^(sat|unsat|unknown)$' "$out" | head -1)"
    if [[ "$verdict" != "sat" ]]; then
        echo "model check ($label): expected sat, got '$verdict'" >&2
        cat "$out" >&2
        exit 1
    fi

    local assertions
    assertions="$(grep '(define-fun' "$out" | \
        sed 's|(define-fun \([^ ]*\) () Int \(.*\))|(assert (= \1 \2))|')"

    local verify="$TMP_DIR/model_${label}_verify.smt2"
    {
        grep -vE '\(check-sat\)|\(exit\)' "$input"
        printf '%s\n' "$assertions"
        echo "(check-sat)"
    } > "$verify"

    local ref_verdict
    ref_verdict="$(z3 "$verify" 2>/dev/null | grep -E '^(sat|unsat|unknown)$' | head -1)"
    if [[ "$ref_verdict" != "sat" ]]; then
        echo "model check ($label): z3 rejected the model (got '$ref_verdict')" >&2
        cat "$verify" >&2
        exit 1
    fi
}

# Default mode: two products (x*y and x*x)
check_model_valid "two_products_default"  "$DATA_DIR/sat_two_products.smt2"
# --uf mode: same formula, model produced by UIF-based solver
check_model_valid "two_products_uf"       "$DATA_DIR/sat_two_products.smt2" --uf

# Default mode: mod with nonlinear divisor
check_model_valid "mod_div_default"       "$DATA_DIR/sat_mod_div.smt2"
# --uf mode: idiv_uf / mod_uf application values must be pinned correctly
check_model_valid "mod_div_uf"            "$DATA_DIR/sat_mod_div.smt2" --uf

# Both SAT instances from the general test suite also produce valid models
check_model_valid "qf0_default"           "$DATA_DIR/qf0.smt2"
check_model_valid "qf0_uf"               "$DATA_DIR/qf0.smt2" --uf
check_model_valid "qf1_default"           "$DATA_DIR/qf1.smt2"
check_model_valid "qf1_uf"               "$DATA_DIR/qf1.smt2" --uf
