; UNSAT purely by congruence: y-z=0 implies x*y = x*z.
; Expressed as (y-z=0) rather than (y=z) so SimplePropagate cannot substitute.
;
; Old solver (default): solved lazily via congruence axiom.
; --no-congruence:       never converges (new x value chosen each iteration).
; --uf:                  UFLIA/EUF derives mul_uf(x,y)=mul_uf(x,z) from y-z=0 directly.
(set-logic QF_NIA)
(declare-fun x () Int)
(declare-fun y () Int)
(declare-fun z () Int)
(assert (= (- y z) 0))
(assert (not (= (* x y) (* x z))))
(check-sat)
