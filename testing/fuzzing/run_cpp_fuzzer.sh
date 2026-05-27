#!/usr/bin/env bash
# Differential fuzzer: qfn2l-cpp vs z3.
# Usage: ./run_cpp_fuzzer.sh <num_tests> [extra nia_fuzzer.py args...]
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <num_tests> [nia_fuzzer.py options...]"
    echo "Example: $0 1000 -j 8 --mdprob 0.2"
    exit 1
fi

its=$1
shift

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Set up a venv with z3-solver for the reference solver.
cd "$SCRIPT_DIR"
if [[ -d venv ]]; then
    source venv/bin/activate
else
    python3 -m venv venv
    venv/bin/pip install z3-solver
    source venv/bin/activate
fi

# FULL_PATH=${HOME}/git/qfn2l/src/qfn2l_cpp/build/qfn2l
FULL_PATH=qfn2l
if ! command -v ${FULL_PATH} &>/dev/null; then
    echo "qfn2l-cpp not found in PATH. Build first with ./configure && make." >&2
    exit 1
fi

S1="${FULL_PATH} --lia-preproc --maxits 20 --timeout 5 --uf"
S2="venv/bin/z3 -T:5 -in"

${FULL_PATH} --version

"$SCRIPT_DIR/nia_fuzzer.py" --no-nia -j 12 "$@" "$its" "$S1" "$S2"
