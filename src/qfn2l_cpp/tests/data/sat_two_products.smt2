; SAT: x*y = 12, x*x <= 25, x > 0, y > 0
; Two distinct products -- tests --uf model correctness.
(set-logic QF_NIA)
(declare-fun x () Int)
(declare-fun y () Int)
(assert (= (* x y) 12))
(assert (<= (* x x) 25))
(assert (> x 0))
(assert (> y 0))
(check-sat)
