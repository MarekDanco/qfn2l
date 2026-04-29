import z3


class QLev:
    """One quantification level in the prefix."""

    def __init__(self, is_forall, vs):
        self._is_forall = is_forall
        self._vs = vs

    def __repr__(self):
        """User-friendly representation."""
        rv = "E" if self.is_exists() else "A"
        for v in self.vars():
            if z3.is_int(v):
                ts = "Z"
            elif z3.is_bool(v):
                ts = "B"
            else:
                ts = str(v.sort())
            rv += f" {v}:{ts}"
        return rv

    def vars(self):
        """Vars getter."""
        return self._vs

    def is_forall(self):
        """Is this a universal quantifier."""
        return self._is_forall

    def is_exists(self):
        """Is this a existential quantifier."""
        return not self._is_forall

    def swap_q(self):
        """Swap the quantifier (mutable operation)."""
        self._is_forall = not self._is_forall

    def add_var(self, v):
        """Adding a var to the current set (mutable operation)."""
        if v in self._vs:
            return
        self._vs.append(v)

    def add_vars(self, vs):
        """Adding vars to the current set (mutable operation)."""
        self._vs += vs


def to_fla(prefix, body):
    fla = body
    for lev in range(len(prefix) - 1, -1, -1):
        qlev = prefix[lev]
        if not qlev.vars():
            continue
        fla = (
            z3.Exists(qlev.vars(), fla)
            if qlev.is_exists()
            else z3.ForAll(qlev.vars(), fla)
        )
    return fla
