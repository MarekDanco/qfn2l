#include "prefix.h"
#include <algorithm>
#include <sstream>

void QLev::add_var(const Term& v) {
    auto it = std::find_if(vars.begin(), vars.end(),
                           [&v](const Term& x) { return x.id() == v.id(); });
    if (it == vars.end()) vars.push_back(v);
}

void QLev::add_vars(const TermVec& vs) {
    for (auto& v : vs) add_var(v);
}

std::string QLev::to_string(const Ctx& ctx) const {
    std::ostringstream oss;
    oss << (is_exists_q ? "E" : "A");
    for (auto& v : vars) {
        oss << " " << v.to_string()
            << ":" << (v.get_sort().is_int() ? "Z" : "B");
    }
    return oss.str();
}

Term to_fla(const Ctx& ctx, const Prefix& prefix, const Term& body) {
    Term fla = body;
    for (int lev = static_cast<int>(prefix.size()) - 1; lev >= 0; --lev) {
        const auto& qlev = prefix[lev];
        if (qlev.vars.empty()) continue;
        z3::expr_vector vars(ctx.zctx);
        for (auto& v : qlev.vars) vars.push_back(v);
        fla = qlev.is_exists_q ? z3::exists(vars, fla) : z3::forall(vars, fla);
    }
    return fla;
}
