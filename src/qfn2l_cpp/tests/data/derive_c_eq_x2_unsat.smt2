(set-info :smt-lib-version 2.6)
(set-logic QF_NIA)
(set-info :status unsat)
(set-info :source |Small consequence extracted from aproveSMT100500964594886141: nonnegative x,z,c with x>0, c>=x^2, x*z>=c, and x*z*(x^2-c)>=0 force c=x^2.|)

(declare-fun x () Int)
(declare-fun z () Int)
(declare-fun c () Int)

(assert (>= x 0))
(assert (>= z 0))
(assert (>= c 0))

;; The nonzero branch from the derivation.
;(assert (> x 0))

;; From the original constraints:
;;   c >= x^2
;;   x*z >= c
;;   x*z*(x^2-c) >= 0
(assert (>= c (* x x)))
(assert (>= (* x z) c))
;(assert (>= (* x z (+ (* x x) (* (- 1) c))) 0))
(assert (>= (- (* x x x z) (* x c z)) 0))

;; Negate the derived consequence c = x^2.
(assert (distinct c (* x x)))

(check-sat)
(exit)
