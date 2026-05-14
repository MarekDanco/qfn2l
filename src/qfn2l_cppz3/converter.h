#pragma once
#include "utils.h"
#include <unordered_map>
#include <utility>

// Convert a formula to Negation Normal Form.
// Quantifiers are handled by extracting bound variables via z3's quantifier
// API, substituting fresh constants, then recursing into the body.
class NNFConverter {
    struct PairHash {
        size_t operator()(const std::pair<Term, bool>& p) const {
            return p.first.hash() ^ (p.second ? 0xdeadbeefUL : 0UL);
        }
    };

  public:
    explicit NNFConverter(const Ctx& ctx) : _ctx(ctx) {}

    Term operator()(const Term& f) { return convert(f, /*negate=*/false); }
    Term convert(const Term& f, bool negate);

  private:
    const Ctx& _ctx;
    std::unordered_map<std::pair<Term, bool>, Term, PairHash> _cache;

    Term to_nnf(const Term& f, bool negate);
    Term to_nnf_inner(const Term& f, bool negate);

    Term flip_cmp(const Term& t);
    Term unchained(const Term& t);
};
