#pragma once
#include "utils.h"
#include <vector>

// One quantifier level in the prefix.
struct QLev {
    bool is_exists_q; // true = exists, false = forall
    smt::TermVec vars;

    QLev(bool is_exists, smt::TermVec vs = {})
        : is_exists_q(is_exists), vars(std::move(vs)) {}

    bool is_forall() const { return !is_exists_q; }
    bool is_exists() const { return is_exists_q; }
    void swap_q() { is_exists_q = !is_exists_q; }

    void add_var(const smt::Term& v);
    void add_vars(const smt::TermVec& vs);
    std::string to_string(const Ctx& ctx) const;
};

using Prefix = std::vector<QLev>;

// Wrap body in quantifiers described by prefix (outermost = prefix[0]).
smt::Term to_fla(const Ctx& ctx, const Prefix& prefix, const smt::Term& body);
