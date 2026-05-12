#include "prefix.h"
#include <algorithm>
#include <sstream>

void QLev::add_var(const smt::Term& v) {
    auto it = std::find(vars.begin(), vars.end(), v);
    if (it == vars.end())
        vars.push_back(v);
}

void QLev::add_vars(const smt::TermVec& vs) {
    for (auto& v : vs)
        add_var(v);
}

std::string QLev::to_string(const Ctx& ctx) const {
    std::ostringstream oss;
    oss << (is_exists_q ? "E" : "A");
    for (auto& v : vars) {
        oss << " " << v->to_string() << ":"
            << (v->get_sort() == ctx.int_sort ? "Z" : "B");
    }
    return oss.str();
}

smt::Term to_fla(const Ctx& ctx, const Prefix& prefix, const smt::Term& body) {
    // TODO: smt-switch quantifier construction may require a different API.
    // Quantifiers are only used transiently during NNF conversion; verify
    // make_term(Forall/Exists, ...) is supported by the chosen backend.
    smt::Term fla = body;
    for (int lev = static_cast<int>(prefix.size()) - 1; lev >= 0; --lev) {
        const auto& qlev = prefix[lev];
        if (qlev.vars.empty())
            continue;
        smt::PrimOp  qop  = qlev.is_exists_q ? smt::Exists : smt::Forall;
        smt::TermVec args = qlev.vars;
        args.push_back(fla);
        fla = ctx.solver->make_term(qop, args);
    }
    return fla;
}
