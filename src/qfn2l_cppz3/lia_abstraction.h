#pragma once
#include "prefix.h"
#include "projections.h"
#include "pures.h"
#include "stats.h"
#include "tagged_logging.h"
#include <optional>
#include <set>
#include <unordered_map>

struct Options {
    int    verbose    = 0;
    int    maxits     = -1;
    int    modax      = 2;
    bool   bounds     = false;
    bool   zeros      = false;
    bool   static_ax  = false;
    int    seed       = 7;
    double timeout    = -1.0;
    double start_time = 0.0;
    int    heur_to    = 3000; // ms
    bool   print_model  = false;
    bool   print_stats  = false;
    bool   brief_stats  = false;
};

class LIAFail : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class LiaAbstraction {
  public:
    LiaAbstraction(const Ctx& ctx, const Options& opts, const Prefix& prefix,
                   const Term& body, bool is_exists);

    void set_level(const TermMap& assignment);

    // Returns model for current-level vars, or nullopt on UNSAT.
    std::optional<TermMap> solve();

    // Verify NIA semantics; add axioms on violation. Returns true iff ok.
    bool check_nia();

    const Term&  current_pure_body() const { return _current_pure_body; }
    const Pures& pures()             const { return _pures; }

    std::optional<Term> get_value(const Term& t) const;

  private:
    // ── Purifier ──────────────────────────────────────────────────────────────
    class Purifier : public TermTransformer {
      public:
        explicit Purifier(LiaAbstraction& parent);
      protected:
        Term visit_node(const Term& t) override;
      private:
        LiaAbstraction&  _parent;
        HasUninterpreted _hu;
        Term visit_idiv(const Term& t);
        Term visit_mod(const Term& t);
        Term visit_mul(const Term& t);
    };

    const Ctx&     _ctx;
    const Options& _opts;
    bool           _is_exists;
    TermVec        _orig_vars;

    Pures            _pures;
    mutable HasUninterpreted _hu;
    Purifier         _purify;

    Prefix _prefix;
    Term   _body;
    Term   _current_body;
    Term   _current_pure_body;
    Term   _current_instantiation;

    std::unordered_map<Term, TermVec, ExprHash, ExprEq> _axioms;

    TermMap _mod_zero_interp;
    TermMap _idiv_zero_interp;

    struct TermPairLess {
        bool operator()(const std::pair<Term, Term>& a,
                        const std::pair<Term, Term>& b) const {
            if (a.first.id() != b.first.id()) return a.first.id() < b.first.id();
            return a.second.id() < b.second.id();
        }
    };
    std::set<std::pair<Term, Term>, TermPairLess> _congruence_pairs_added;

    bool _in_init             = true;
    bool _lia_sat             = false;
    bool _heuristic_left_unsat = false;

    TermMap _prev_var_hints;

    void add_axiom(const Term& pure, const Term& ax, const char* tag = "");
    void add_axioms(const Term& pure, const TermVec& axs, const char* tag = "");

    Term make_pure_constant(const Term& term);
    std::string make_fancy_name(const Term& term) const;

    void _solve();

    std::optional<TermMap> incorporate_assumptions(TermVec& assumptions,
                                                    const char* msg);
    void apply_zeros_heuristic(const TermSet& cur_pures, TermVec& assumptions);
    void apply_bounds_heuristic(const TermSet& cur_pures,
                                const TermVec& zero_assumptions);

    struct MulSplit {
        Term              coeff;
        std::vector<TermVec> pows;
    };
    MulSplit split_mul(const Term& t) const;

    TermVec mk_pow_axioms(const Term& pure, const MulSplit& split);
    TermVec mk_mixed_mul_axioms(const Term& t, const Term& pure,
                                const MulSplit& split);
    TermVec mk_mul_axioms(const Term& t);
    TermVec mk_mod_axiom(const Term& t);
    TermVec mk_idiv_axiom(const Term& t);

    TermVec congruence_axioms_for_pair(const Term& a, const Term& b);
    void    add_lazy_congruence_axioms(const CollectPures& pcol);

    bool is_okay(const Term& pure, const Term& t);
};
