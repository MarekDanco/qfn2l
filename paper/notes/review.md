# Paper review notes

## Bugs / clear mistakes

**`conclusion.tex` line 1: wrong label.**
`\label{sec:related}` but the section is "Conclusion and Future Work".
Leftover from when there was a separate related work section.
Should be `\label{sec:conclusion}`.

**~~`algorithm.tex` line 95: congruence parenthetical is trivially true.~~** *(fixed)*

**~~`algorithm.tex`, Check-Nia algorithm: `\hat\varphi` used but not in scope.~~** *(fixed: added `\hat\varphi` as first parameter)*

## Potentially weird / worth double-checking

**~~`intro.tex`: two "In this paper" paragraphs back to back.~~** *(fixed: second changed to "We revisit...")*

**`intro.tex`: "neutrality" and "proportionality" mentioned but never explained.**
Line 40 lists five axiom types from prior work including "neutrality" and
"proportionality". The algorithm section (line 121-122) says "We retain sign,
zero, and tangent-plane axioms" — so these two are dropped. But nowhere does
the paper define or explain what they are. A reader may wonder what was dropped.

**`background.tex`: `\varphi[\mu]` notation introduced but apparently unused.**
The substitution paragraph defines application `\varphi[\mu]` but this notation
does not appear anywhere else in the paper. If it is truly unused the definition
is dead weight; if it is used somewhere, the reference was missed.

**`background.tex`: `\xs` (vector of variables) defined but lightly used.**
Used only in the monomial definition at the end of background. Could simplify
to just write the product directly without the macro if it only appears once.

**`experiments.tex` line 38: "585--587" range needs explanation.**
The text says "\solver solves 585--587 of these (53%)" but doesn't explain why
there is a range. This comes from the three configurations (table shows 585,
585, 587) but a reader who hasn't noticed that will find the range confusing.
Consider saying "depending on configuration" or citing the table explicitly.

**`experiments.tex` UF remark: "fresh integer variable" is slightly imprecise.**
Ackermann encoding introduces a fresh *constant* per distinct application,
not a "variable" in the logical sense. Minor but might confuse a reader who
distinguishes the two.

**`background.tex`: QF_UFLIA introduced but only used for comparison with prior work.**
The background formally defines QF_UFLIA (line 6-13) and the model paragraph
mentions it, but the paper never actually uses QF_UFLIA — our solver works
in QF_LIA. It's there because we compare against Cimatti et al.'s UFLIA
abstraction. Worth flagging as potentially confusing for a reader who might
expect QF_UFLIA to be solved.

## Style

**`experiments.tex` line 15-16: "the first two run without congruence" is ambiguous.**
Which two? Frontier and base, as listed, but the phrasing could suggest the
last two (Frontier and Congr.). Rewriting as "the base and Frontier
configurations run without congruence axioms" would be clearer.
