supports_colors() {
    case "$TERM" in
        dumb|"") return 1 ;;
        *) return 0 ;;
    esac
}

# Usage
if supports_colors; then
   GREEN="\033[0;32m"
   RED="\033[0;31m"
   NC="\033[0m"  # No Color
else
   GREEN=""
   RED=""
   NC=""
fi

setup_venv() {
    if [[ -d "venv" ]]; then
        echo "Found existing venv. Activating..."
    else
        echo "No venv found. Creating a new one..."
        python3 -m venv venv || return 1
        # Ensure pip is up to date and install dependency
        venv/bin/pip install --upgrade pip
        venv/bin/pip install z3-solver
    fi

    # Activate the virtual environment
    source venv/bin/activate || return 1

}



run_and_check() {
    local program="$1"
    local arg="$2"
    local file="$3"
    local expected="$4"

    echo -n $file ': '

    # Run the program on the file and check stdout for expected
    if "$program" $arg "$file" 2>/dev/null | grep -q $expected;then
        echo "${GREEN}success${NC}"
    else
        echo "${RED}failure${NC}"
    fi
}

setup_venv
S=../src/qfn2l/qf_solver.py
for opts in "--zeros" "--bounds" "--zeros --bounds --modax 5" ; do
	echo OPTS: ${opts}
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard.c_2.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard.c_3.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_0.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_1.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_2.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_3.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_4.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./STC_0019.smt2 sat
	run_and_check "${S}" "--timeout 10 ${opts}" ./STC_0072.smt2 sat
done

