# Cheap Model Fix

Let us denote $[t]$ a *purified* or *abstract* version of $t$, meaning a fresh
variable that represents a nonlinear term $t$. The variable $[t]$ no longer
carries the semantics of $t$ and this needs to be axiomatized. We extend a
notation as $[F]$ meaning that all nonlinear terms in the formula $F$ were
replaced by theire purified version. We say that $[F]$ is an *abstraction* of
$F$.

Here we want to develop a technique that tries to identify cheap techniques to
fix a model of the purified formula $F$ w.r.t.\ the appropriate semantics.
Recall that $F$ is in NNF.

## Implicant

Given a assignment $A$ that is a model of $[F]$ calculate an *implicant*, which
are literals $L$ of $[F]$ s.t. $L\models [F]$. There may be multiple ones but
we pick one arbitrarily, e.g. $x>0\lor y>3$ with $x=y=10$ has two possible
implicants $\{x>0\}$ and $\{y>3\}$. Identify the set of pures $W$ that appear
in $L$ and their values are incorrect, e.g. if $A$ sets $x=y=[xy]=2$, then the
value of $[xy]$ is incorrect. If the value of $[t]$ is incorrect and a variable
$x$ appears in $t$ but does not appear anywhere else in $L$, then we say that
$x$ is *adjustable*. 

In the first iteration find adjustable variables and bring them along with all
the other types of information, $L$, $W$. Recall that CheckVal defined pures.h#L52 already gets us the implicant.

# Fixing
Let $t_v$ be the current value of $[t]$.

* If $t=x^ky^1$ and both $x$ and $y$ are adjustable, set $x=1$ and $y$ to value of $t$.
* If $t=x^ky^l$ and both $x$ and $y$ are adjustable, check if $t$ is $k$-th or $l$-th power of some integer and set $x$ or $y$ accordingly if it succeeds.
* If $t=x^ky^l$ and $x$ is adjustable, $y$ is not adjustable. Assuming $y_v$ is
the value of $y^l$, check if $t_v$ is divisible by $y_l$ and if so, if
$t_v/y_l$ is a $k$-th power of an integer.

