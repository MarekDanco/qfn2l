#pragma once
#include "prefix.h"
#include "pures.h"
#include <optional>
#include <set>
#include <unordered_map>

struct Options {
    int verbose = 0;
    int maxits = -1;
    int modax = 2;
    bool bounds = false;
    int64_t bounds_initial = 5;
    bool zeros = false;
    bool static_ax = false;
    int seed = 7;
    double timeout = -1.0;
    double start_time = 0.0;
    int heur_to = 3000; // ms
    bool print_model = false;
    bool print_stats = false;
    bool brief_stats = false;
    bool preprocess = false;
    int preprocess_aggressive = 0;
    int preprocess_aggressive_timeout = 5000;
    std::string backend = "z3"; // "z3" | "cvc5"
};

class LIAFail : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class LiaAbstraction {
  public:
    LiaAbstraction(const Ctx& ctx, const Options& opts, const Prefix& prefix,
                   const smt::Term& body, bool is_exists);

    // Instantiate the abstraction under the given assignment (outer variables).
    void set_level(const smt::UnorderedTermMap& assignment);

    // Solve the current LIA instantiation.
    // Returns the model terms for the current level's variables, or empty on UNSAT.
    // Throws LIAFail on z3::unknown.
    std::optional<smt::UnorderedTermMap> solve();

    // Check NIA correctness; add axioms on violation.
    // Returns true iff no violations found.
    bool check_nia();

    const smt::Term& current_pure_body() const { return _current_pure_body; }
    const Pures& pures() const { return _pures; }

    // Value of a pure/variable in the current model (nullopt if not assigned).
    std::optional<smt::Term> get_value(const smt::Term& t) const;

    // The solver used for LIA checks (same as ctx.solver, accessed via push/pop).
    const smt::SmtSolver& lia_solver() const { return _ctx.solver; }

  private:
    // ── Purifier ──────────────────────────────────────────────────────────────
    class Purifier : public TermTransformer {
      public:
        explicit Purifier(LiaAbstraction& parent);

      protected:
        smt::Term visit_node(const smt::Term& t) override;

      private:
        LiaAbstraction& _parent;
        HasUninterpreted _hu;
        smt::Term visit_idiv(const smt::Term& t);
        smt::Term visit_mod(const smt::Term& t);
        smt::Term visit_mul(const smt::Term& t);
    };

    const Ctx& _ctx;
    const Options& _opts;
    bool _is_exists;
    smt::TermVec _orig_vars;

    Pures _pures;
    mutable HasUninterpreted _hu;
    Purifier _purify;

    Prefix _prefix;
    smt::Term _body;
    smt::Term _current_body;
    smt::Term _current_pure_body;
    smt::Term _current_instantiation;

    // axioms[pure] = list of axiom formulas
    std::unordered_map<smt::Term, smt::TermVec> _axioms;

    // Interpretation of division-by-zero cases.
    smt::UnorderedTermMap _mod_zero_interp;
    smt::UnorderedTermMap _idiv_zero_interp;

    // Track congruence pairs already added.
    std::set<std::pair<smt::Term, smt::Term>> _congruence_pairs_added;

    bool _in_init = true; // suppresses logging until intermediate pures are pruned
    bool _lia_sat = false;
    // Set by incorporate_assumptions when it ends with a check_sat_assuming UNSAT.
    // Signals that the solver state must be restored before get_value() is valid.
    bool _heuristic_left_unsat = false;

    // Stores original-variable values from the previous SAT model.
    // Used to warm-start the next LIA call via set_initial_value, encouraging
    // variable stability and faster convergence of gap axioms.
    smt::UnorderedTermMap _prev_var_hints;

    void add_axiom(const smt::Term& pure, const smt::Term& ax, const char* tag = "");
    void add_axioms(const smt::Term& pure, const smt::TermVec& axs,
                    const char* tag = "");

    smt::Term make_pure_constant(const smt::Term& term);
    std::string make_fancy_name(const smt::Term& term) const;

    void _solve();

    // Heuristics.
    std::optional<smt::UnorderedTermMap>
    incorporate_assumptions(smt::TermVec& assumptions, const char* msg);
    void apply_zeros_heuristic(const smt::UnorderedTermSet& cur_pures,
                               smt::TermVec& assumptions);

    // Axiom generation.
    struct MulSplit {
        smt::Term coeff;
        std::vector<smt::TermVec> pows;
    };
    MulSplit split_mul(const smt::Term& t) const;

    smt::TermVec mk_pow_axioms(const smt::Term& pure, const MulSplit& split);
    smt::TermVec mk_mixed_mul_axioms(const smt::Term& t, const smt::Term& pure,
                                     const MulSplit& split);
    smt::TermVec mk_mul_axioms(const smt::Term& t);
    smt::TermVec mk_mod_axiom(const smt::Term& t);
    smt::TermVec mk_idiv_axiom(const smt::Term& t);

    // Congruence axioms.
    smt::TermVec congruence_axioms_for_pair(const smt::Term& a, const smt::Term& b);
    void add_lazy_congruence_axioms(const CollectPures& pcol);

    bool is_okay(const smt::Term& pure, const smt::Term& t);
};
