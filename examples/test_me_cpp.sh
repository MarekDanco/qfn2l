supports_colors() {
    case "$TERM" in
        dumb|"") return 1 ;;
        *) return 0 ;;
    esac
}

if supports_colors; then
   GREEN="\033[0;32m"
   RED="\033[0;31m"
   NC="\033[0m"
else
   GREEN=""
   RED=""
   NC=""
fi

run_and_check() {
    local program="$1"
    local arg="$2"
    local file="$3"
    local expected="$4"

    echo -n $file ': '

    if "$program" $arg "$file" 2>/dev/null | grep -q $expected; then
        echo "${GREEN}success${NC}"
    else
        echo "${RED}failure${NC}"
    fi
}

S=../src/qfn2l_cpp/build/qfn2l
for opts in " "  "--zeros"  "--preproc-aggressive 2"  "--bounds" ; do
	echo OPTS: ${opts}
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard.c_2.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard.c_3.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_0.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_1.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_2.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_3.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./hard-ll.c_4.smt2 unsat
	run_and_check "${S}" "--timeout 10 ${opts}" ./STC_0011.smt2 sat
	run_and_check "${S}" "--timeout 10 ${opts}" ./STC_0019.smt2 sat
	run_and_check "${S}" "--timeout 10 ${opts}" ./STC_0072.smt2 sat
	run_and_check "${S}" "--timeout 10 ${opts}" ./STC_0504.smt2 sat
	if [ "${opts}" != "--zeros" ] && [ "${opts}" != " " ]; then
	  run_and_check "${S}" "--timeout 10 ${opts}" ./aproveSMT867442185995558133.smt2 sat
	  run_and_check "${S}" "--timeout 10 ${opts}" ./aproveSMT9181010827166665933.smt2 sat
	fi
done
