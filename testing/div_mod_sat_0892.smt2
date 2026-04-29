(set-logic NIA)

(assert (forall ((n Int)) (exists ((x Int)) (= (mod (* x x x x x x x) 10) (mod n 10)))))

(check-sat)
