#!/bin/bash
# Author:  mikolas
# Created on:  Sun Dec 21 08:48:48 AM CET 2025
# Copyright (C) 2025, Mikolas Janota
#
setup_venv() {
    if [[ -d "venv" ]]; then
        echo "Found existing venv. Activating..."
    else
        echo "No venv found. Creating a new one..."
        python3 -m venv venv || return 1
        # Ensure pip is up to date and install dependency
        # venv/bin/pip install --upgrade pip
        venv/bin/pip install z3-solver
    fi

    # Activate the virtual environment
    source venv/bin/activate || return 1

}


if [[ $# -ne 1 ]] ; then
    echo "Usage: $0 <a number of iterations>"
    exit 1
fi

its=$1
setup_venv
S1='../../src/qfn2l/qf_solver.py --maxits 20 --timeout 5'
S2='./venv/bin/z3 -T:5 -in'
./nia_fuzzer.py --no-nia -j 10 ${its} "$S1" "$S2"
