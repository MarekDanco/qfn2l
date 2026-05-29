# Paper TODOs

## Open notes / questions

**Yices** (`experiments.tex`): The `\marek{what do we know about yices?}` note is still
in the experiments prose. Either add a brief characterisation of Yices or just remove the
note and leave Yices without comment (it is a well-known solver and doesn't need
justification).

## Bib entries present but only used in commented-out related work

The following cite keys are defined in `refs.bib` and referenced only inside the
commented-out related work block in `conclusion.tex`. They are not currently needed but
should be kept in the bib in case the related work section is reinstated:

- `jovanovic-mcsat`
- `jovanovic-moura`
- `kremer2020`
- `smtrat`

## Commented-out related work

`conclusion.tex` contains a commented-out `\paragraph{Related work.}` block covering
bit-blasting (Z3, SMT-RAT), sign-based case analysis (Jovanovic & de Moura), CVC5 hybrid
approach, and MCSAT/DPLL(T) methods (Yices 2). Consider whether to reinstate this as a
separate Related Work section before submission.
