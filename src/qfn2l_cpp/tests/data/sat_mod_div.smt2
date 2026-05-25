; SAT: mod and div with nonlinear divisor x*y
; Tests --uf model correctness for idiv_uf / mod_uf.
(set-logic QF_NIA)
(declare-fun x () Int)
(declare-fun y () Int)
(declare-fun z () Int)
(assert (> x 0))
(assert (> y 0))
(assert (= (mod z (* x y)) 3))
(assert (> z 10))
(assert (< (* x y) 10))
(check-sat)
