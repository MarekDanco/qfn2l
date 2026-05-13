#pragma once
#include "utils.h"
#include <unordered_map>
#include <utility>

// Convert a formula to Negation Normal Form.
// Quantifiers are handled by extracting bound variables and substituting
// fresh symbols, then recursing into the body.
class NNFConverter {
    struct PairHash {
        size_t operator()(const std::pair<smt::Term, bool>& p) const {
            return p.first->hash() ^ (p.second ? 0xdeadbeefUL : 0UL);
        }
    };

  public:
    explicit NNFConverter(const Ctx& ctx) : _ctx(ctx) {}

    smt::Term operator()(const smt::Term& f) { return convert(f, /*negate=*/false); }

    smt::Term convert(const smt::Term& f, bool negate);

  private:
    const Ctx&                                                          _ctx;
    std::unordered_map<std::pair<smt::Term, bool>, smt::Term, PairHash> _cache;

    smt::Term to_nnf(const smt::Term& f, bool negate);
    smt::Term to_nnf_inner(const smt::Term& f, bool negate);

    smt::Term flip_cmp(const smt::Term& t);
    smt::Term unchained(const smt::Term& t);
};
