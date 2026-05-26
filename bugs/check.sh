#!/bin/bash

for f in *.smt2; do
	../testing/check_model_cpp.py $f --ref-solver ~/solvers/cvc5/build/bin/cvc5 -- --bounds --tangent --no-congruence --model-fix -pa 2 --timeout 180;
done
