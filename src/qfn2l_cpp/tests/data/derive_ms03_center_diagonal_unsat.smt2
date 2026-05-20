(set-info :smt-lib-version 2.6)
(set-logic QF_NIA)
(set-info :status unsat)
(set-info :source |Small consequence extracted from MS_03: in a 3x3 magic square of squares, the common sum constraints force x_0_0^2 + x_2_2^2 = 2*x_1_1^2.|)

(declare-fun t () Int)
(declare-fun x_0_0 () Int)
(declare-fun x_0_1 () Int)
(declare-fun x_0_2 () Int)
(declare-fun x_1_0 () Int)
(declare-fun x_1_1 () Int)
(declare-fun x_1_2 () Int)
(declare-fun x_2_0 () Int)
(declare-fun x_2_1 () Int)
(declare-fun x_2_2 () Int)

;; The eight magic-square sum constraints from MS_03.
(assert (= (+ (* x_0_0 x_0_0) (* x_0_1 x_0_1) (* x_0_2 x_0_2)) t))
(assert (= (+ (* x_1_0 x_1_0) (* x_1_1 x_1_1) (* x_1_2 x_1_2)) t))
(assert (= (+ (* x_2_0 x_2_0) (* x_2_1 x_2_1) (* x_2_2 x_2_2)) t))
(assert (= (+ (* x_0_0 x_0_0) (* x_1_0 x_1_0) (* x_2_0 x_2_0)) t))
(assert (= (+ (* x_0_1 x_0_1) (* x_1_1 x_1_1) (* x_2_1 x_2_1)) t))
(assert (= (+ (* x_0_2 x_0_2) (* x_1_2 x_1_2) (* x_2_2 x_2_2)) t))
(assert (= (+ (* x_0_0 x_0_0) (* x_1_1 x_1_1) (* x_2_2 x_2_2)) t))
(assert (= (+ (* x_0_2 x_0_2) (* x_1_1 x_1_1) (* x_2_0 x_2_0)) t))

;; Negate the derived diagonal-pair identity.
(assert (distinct (+ (* x_0_0 x_0_0) (* x_2_2 x_2_2))
                  (* 2 x_1_1 x_1_1)))

(check-sat)
(exit)
