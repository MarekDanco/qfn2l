#include "qf_solver.h"
#include "converter.h"
#include "visitors.h"
#include "stats.h"
#include "tagged_logging.h"

static const char* LOG_TAG = "slv";

QfSolver::QfSolver(const Ctx& ctx, const Options& opts,
                   const smt::Term& formula)
    : _ctx(ctx)
    , _opts(opts)
{
    STATS.begin_phase(STATS.nnf_time);
    smt::Term f = NNFConverter(ctx)(formula);
    STATS.end_phase();

    STATS.begin_phase(STATS.propagate_time);
    f = SimplePropagate(ctx)(f);
    STATS.end_phase();

    smt::TermVec free_vars = get_vars(f);
    Prefix prefix = {QLev(true, free_vars)};

    STATS.begin_phase(STATS.makedefs_time);
    auto [new_prefix, new_f] = MakeDefs(ctx).make(prefix, f);
    STATS.end_phase();

    LOG(LOG_TAG, 3, "input ready, %zu prefix levels", new_prefix.size());

    _level_info  = std::make_unique<FormulaInfo>(ctx, std::move(new_prefix), new_f);
    _abstraction = std::make_unique<LiaAbstraction>(ctx, opts, *_level_info,
                                                    /*is_exists=*/true);
}

std::optional<bool> QfSolver::solve() {
    _abstraction->set_level(0, {});
    while (true) {
        if (_opts.maxits >= 0 && STATS.its >= _opts.maxits)
            return std::nullopt;
        STATS.its += 1;
        LOG(LOG_TAG, 1, "it: %ld", STATS.its.value);

        std::optional<smt::UnorderedTermMap> model;
        try {
            model = _abstraction->solve();
        } catch (const LIAFail&) {
            return std::nullopt;
        }

        LOG(LOG_TAG, 2, "model: %s", model ? "sat" : "unsat");
        if (!model) return false;
        if (g_verbosity >= 2) {
            for (auto& [c, v] : *model)
                LOG(LOG_TAG, 2, "  %s = %s",
                    c->to_string().c_str(), v->to_string().c_str());
        }

        bool nia_ok = _abstraction->check_nia();
        LOG(LOG_TAG, 2, "nia ok: %s", nia_ok ? "true" : "false");
        if (nia_ok) return true;

        _abstraction->set_level(0, {});
    }
}
