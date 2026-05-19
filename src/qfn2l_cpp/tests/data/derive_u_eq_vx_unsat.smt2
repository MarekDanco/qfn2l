(set-info :smt-lib-version 2.6)
(set-logic QF_NIA)
(set-info :status unsat)
(set-info :source |Small consequence extracted from aproveSMT100500964594886141: nonnegative x,z,u,v with x>0, z=x, u>=v*z, and x*z*(v*x-u)>=0 force u=v*x.|)

(declare-fun x () Int)
(declare-fun z () Int)
(declare-fun u () Int)
(declare-fun v () Int)

(assert (>= x 0))
(assert (>= z 0))
(assert (>= u 0))
(assert (>= v 0))

;; The nonzero branch from the derivation.
(assert (> x 0))
(assert (= z x))

;; From the original constraints:
;;   u >= v*z
;;   x*z*(v*x-u) >= 0
(assert (>= u (* v z)))
(assert (>= (* x z (+ (* v x) (* (- 1) u))) 0))

;; Negate the derived consequence u = v*x.
(assert (distinct u (* v x)))

(check-sat)
(exit)
