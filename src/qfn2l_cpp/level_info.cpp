#include "level_info.h"

// ── GetLevel ──────────────────────────────────────────────────────────────────
GetLevel::GetLevel(
    const Ctx& ctx,
    const std::unordered_map<smt::Term, int>& const2lev,
    const smt::Term& root)
    : _ctx(ctx), _const2lev(const2lev)
{
    (*this)(root);
}

int GetLevel::operator()(const smt::Term& t) {
    auto it = _memo.find(t);
    if (it != _memo.end()) return it->second;
    int lev = compute(t);
    _memo[t] = lev;
    if (lev >= 0 && !is_nnf_connective(t))
        _terms[t] = lev;
    return lev;
}

int GetLevel::compute(const smt::Term& t) {
    if (t->begin() == t->end()) {  // leaf
        auto it = _const2lev.find(t);
        return it != _const2lev.end() ? it->second : -1;
    }
    int max_lev = 0;
    for (auto cit = t->begin(); cit != t->end(); ++cit)
        max_lev = std::max(max_lev, (*this)(*cit));
    return max_lev;
}

// ── FormulaInfo ───────────────────────────────────────────────────────────────
FormulaInfo::FormulaInfo(const Ctx& ctx, Prefix p, smt::Term b)
    : prefix(std::move(p))
    , body(std::move(b))
    , _ctx(ctx)
    , _get_level(ctx, _const2lev, body)
{
    for (int lev = 0; lev < static_cast<int>(prefix.size()); ++lev)
        for (auto& v : prefix[lev].vars)
            _const2lev[v] = lev;
}

void FormulaInfo::add_const(const smt::Term& c, int lev) {
    _const2lev[c] = lev;
}

int FormulaInfo::get_level(const smt::Term& t) {
    return _get_level(t);
}

const std::unordered_map<smt::Term, int>& FormulaInfo::get_terms() {
    return _get_level.terms();
}
