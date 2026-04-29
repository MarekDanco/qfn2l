#!/bin/bash
#
# File:  compare.sh
# Author:  mikolas
# Created on:  Tue Jan 6 01:54:27 PM UTC 2026
# Copyright (C) 2026, Mikolas Janota
#
if [[ $# -gt 1 ]] ; then
    echo "Usage: $0 [bug file]"
    exit 1
fi
if [[ $# -eq 1 ]] ; then
    bug_file=$1
else
    bug_file=${bug_file}
fi

ulimit -t 10
r1=`z3 ${bug_file} | grep -i -e '^sat$' -e '^unsat$'`
r2=`../../../src/n2l/solver.py --maxbts 10 --maxits 20 ${bug_file} | grep -i -e '^sat$' -e '^unsat$'`
echo z3:$r1 n2l:$r2
[ "$r1" != "$r2" ]
