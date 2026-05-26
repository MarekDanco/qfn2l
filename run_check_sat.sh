#!/usr/bin/env bash
set -euo pipefail

REPO=/home/marek/phd/2025-2026/qfn2l
SAT_PROBLEMS=$REPO/sat-problems
CHECK_MODEL=$REPO/testing/check_model_cpp.py
REF_SOLVER=/local/home/dancomar/cvc5/build/bin/cvc5
LOG=$REPO/check_sat.log

find -L "$SAT_PROBLEMS" -name "*.smt2" \
    | parallel --line-buffer -j100 \
        python3 "$CHECK_MODEL" --ref-solver "$REF_SOLVER" {} \
        -- --bounds --tangent --no-congruence --model-fix --timeout 180 \
        :::: - \
    >>"$LOG" 2>&1

echo "done" >>"$LOG"
