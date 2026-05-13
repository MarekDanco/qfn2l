# Z3 backend: distinct integer numerals can compare equal as `smt::Term`

## Summary

In the Z3 backend, distinct integer numeral terms can compare equal through
`smt::Term::operator==`. I reproduced this with `9` and `-9`: the terms are
semantically distinct, but `pos9 == neg9` returns true. This also causes
`smt::UnorderedTermSet` and `smt::UnorderedTermMap` to collapse the two terms
as if they were the same key.

## Reproducer

```cpp
#include "smt.h"
#include "z3_factory.h"

#include <cassert>

int main() {
    smt::SmtSolver solver = smt::Z3SolverFactory::create(false);
    smt::Sort int_sort = solver->make_sort(smt::INT);

    smt::Term pos9 = solver->make_term(int64_t(9), int_sort);
    smt::Term neg9 = solver->make_term(int64_t(-9), int_sort);

    assert(pos9 != neg9);

    smt::UnorderedTermSet set;
    set.insert(pos9);
    set.insert(neg9);
    assert(set.size() == 2);

    smt::UnorderedTermMap map;
    map[pos9] = pos9;
    map[neg9] = neg9;
    assert(map.size() == 2);
}
```

## Observed Behavior

The first assertion fails:

```text
9 vs -9: terms compare equal
```

In my downstream test, this also means `UnorderedTermSet` and
`UnorderedTermMap` cannot safely distinguish the two terms.

## Expected Behavior

`9` and `-9` should compare distinct as `smt::Term` objects. Hash collisions
are acceptable for hash-table bucketing, but equality should not be based only
on hash equality.

## Suspected Cause

In `z3/src/z3_term.cpp`, `Z3Term::compare` appears to compare Z3 hashes:

```cpp
bool Z3Term::compare(const Term & absterm) const
{
  std::shared_ptr<Z3Term> zs = std::static_pointer_cast<Z3Term>(absterm);
  if (is_function && zs->is_function)
  {
    return z_func.hash() == (zs->z_func).hash();
  }
  else if (!is_function && !zs->is_function)
  {
    return term.hash() == (zs->term).hash();
  }
  return false;
}
```

This makes `operator==` vulnerable to Z3 hash collisions. The local smt-switch
checkout used for this repro was commit `bfe3761`.

## Downstream Impact

Downstream code that uses `smt::Term` as keys in `smt::UnorderedTermSet` or
`smt::UnorderedTermMap` can merge distinct arithmetic constants. This can break
substitution maps, pures/axiom tables, visited sets, and other memoization that
assumes `smt::Term` equality is semantic identity.
