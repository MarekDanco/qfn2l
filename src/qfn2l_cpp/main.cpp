#include "qf_solver.h"
#include "stats.h"
#include "tagged_logging.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/time.h>
#include <unistd.h>

#ifdef BACKEND_Z3
#include "z3.h"
#include "z3_factory.h"
#include "z3_solver.h"
#include "z3_term.h"
#endif
#ifdef BACKEND_CVC5
#include "cvc5_factory.h"
#endif

// ── Signal handling ───────────────────────────────────────────────────────────
static bool g_print_stats = false;
static bool g_brief_stats = false;
static double g_start_time = 0.0;

static void handle_signal(int) {
    // TODO: Make signal handling async-signal-safe: set a flag here and do
    // stats/stdio work from normal control flow.
    STATS.commit_phases();
    STATS.total_time.value += std::chrono::duration<double>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count() -
                              g_start_time;
    if (g_brief_stats) {
        STATS.brief_prn();
        std::printf("\n");
    } else if (g_print_stats) {
        STATS.prn();
        std::printf("\n");
    }
    std::printf("unknown\n");
    std::fflush(stdout);
    std::_Exit(0);
}

// ── Argument parsing ──────────────────────────────────────────────────────────
static void print_usage(const char* prog) {
    std::printf(
        "Usage: %s [options] [file.smt2|-]\n\n"
        "Options:\n"
        "  -v N                  Verbosity level (default 0)\n"
        "  --maxits N            Max iterations (-1 = unlimited)\n"
        "  --modax N             Modulo axioms up to N (default 2, <=1 disables)\n"
        "  --bounds              Fast small-model heuristic: try ±initial bound (few rounds) before unbounded\n"
        "  --bounds-initial N    Initial bound magnitude for --bounds (default 5)\n"
        "  --zeros               Try setting mul pures to 0\n"
        "  --static              Add static axioms for div/mod\n"
        "  --seed N              Random seed (default 7)\n"
        "  --timeout N           Wall-clock timeout in seconds (-1 = none)\n"
        "  --heur-timeout N      Heuristic LIA timeout in ms (default 3000)\n"
        "  --lia-preproc         Preprocess LIA formula with z3 simplify+propagate before each solve\n"
        "  --tangent             Tangent plane axioms for x*y products\n"
        "  --frontier            Frontier strategy for tangent lemmas (requires --tangent)\n"
        "  --model-fix           Try cheap model repairs for adjustable mul factors\n"
        "  --model-fix2          Like --model-fix but sub-iterates until NIA check succeeds\n"
        "  -p, --preproc         Preprocess with Z3 tactics\n"
        "  -pa N, --preproc-aggressive N  Z3 tactic preprocessing level (1 or 2)\n"
        "  -pt N, --preproc-timeout N     Timeout per tactic for -p/-pa in ms (default "
        "5000)\n"
        "  --dump-qf-nia FILE     Dump the post-preprocessing QF_NIA formula\n"
        "  --dump-abstraction FILE  Dump the LIA abstraction formula sent to the solver\n"
        "  --print-model         Print SAT model as define-fun lines\n"
        "  --stats               Print full stats on exit\n"
        "  --brief-stats         Print brief stats on exit\n"
        "  --backend NAME        Solver backend: z3 (default) | cvc5\n"
        "  --no-congruence       Disable lazy congruence axioms (reduces overhead with many pures)\n"
        "  --help                Show this help\n",
        prog);
}

static Options parse_args(int argc, char** argv, std::string& filename) {
    Options opts;
    filename = "-";
    if (argc == 1 && isatty(STDIN_FILENO)) {
        print_usage(argv[0]);
        std::exit(0);
    }
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing argument for %s\n", arg.c_str());
                std::exit(1);
            }
            return argv[++i];
        };
        if (arg == "-v" || arg == "--verbose")
            g_verbosity = std::stoi(next());
        else if (arg == "--maxits")
            opts.maxits = std::stoi(next());
        else if (arg == "--modax")
            opts.modax = std::stoi(next());
        else if (arg == "--bounds")
            opts.bounds = true;
        else if (arg == "--bounds-initial")
            opts.bounds_initial = std::stoll(next());
        else if (arg == "--zeros")
            opts.zeros = true;
        else if (arg == "--static")
            opts.static_ax = true;
        else if (arg == "--seed")
            opts.seed = std::stoi(next());
        else if (arg == "--timeout")
            opts.timeout = std::stod(next());
        else if (arg == "--heur-timeout")
            opts.heur_to = std::stoi(next());
        else if (arg == "--print-model")
            opts.print_model = true;
        else if (arg == "--stats")
            opts.print_stats = true;
        else if (arg == "--brief-stats")
            opts.brief_stats = true;
        else if (arg == "--lia-preproc")
            opts.lia_preproc = true;
        else if (arg == "--tangent")
            opts.tangent = true;
        else if (arg == "--frontier")
            opts.frontier = true;
        else if (arg == "--model-fix")
            opts.model_fix = true;
        else if (arg == "--model-fix2")
            opts.model_fix2 = true;
        else if (arg == "-p" || arg == "--preproc")
            opts.preprocess = true;
        else if (arg == "-pa" || arg == "--preproc-aggressive")
            opts.preprocess_aggressive = std::stoi(next());
        else if (arg == "-pt" || arg == "--preproc-timeout")
            opts.preprocess_aggressive_timeout = std::stoi(next());
        else if (arg == "--dump-qf-nia")
            opts.dump_qf_nia_formula = next();
        else if (arg == "--dump-abstraction")
            opts.dump_abstraction_formula = next();
        else if (arg == "--backend")
            opts.backend = next();
        else if (arg == "--no-congruence")
            opts.congruence = false;
        else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg[0] != '-')
            filename = arg;
        else {
            // TODO: Accept explicit "-" as stdin; usage currently advertises it.
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            std::exit(1);
        }
    }
    return opts;
}

// ── Solver factory ────────────────────────────────────────────────────────────
static smt::SmtSolver create_solver(const std::string& backend) {
#ifdef BACKEND_CVC5
    if (backend == "cvc5") {
        auto s = smt::Cvc5SolverFactory::create(false);
        return s;
    }
#endif
#ifdef BACKEND_Z3
    // TODO: Reject unknown or unavailable backend names instead of silently
    // falling back to Z3.
    {
        (void)backend;
        auto s = smt::Z3SolverFactory::create(false);
        return s;
    }
#else
    std::fprintf(stderr, "No backend compiled in. Rebuild with -DBACKEND_Z3=ON.\n");
    std::exit(1);
#endif
}

static void print_backend_version(const std::string& backend) {
#ifdef BACKEND_Z3
    if (backend == "z3") {
        unsigned major = 0, minor = 0, build = 0, revision = 0;
        Z3_get_version(&major, &minor, &build, &revision);
        std::fprintf(stderr, "backend: z3 %u.%u.%u.%u\n", major, minor, build,
                     revision);
        return;
    }
#endif
#ifdef BACKEND_CVC5
    if (backend == "cvc5") {
        std::fprintf(stderr, "backend: cvc5 (version unavailable)\n");
        return;
    }
#endif
    std::fprintf(stderr, "backend: %s (version unavailable)\n", backend.c_str());
}

// ── Z3 tactic preprocessing ───────────────────────────────────────────────────
#ifdef BACKEND_Z3
struct PreprocessResult {
    smt::Term formula;
    // Subgoals in application order (one per tactic that succeeded and produced
    // exactly one subgoal). goal_chain[i].convert_model(m) converts a model of
    // goal_chain[i]'s formula back to a model of the preceding (parent) goal.
    // Apply in reverse order to recover values of eliminated variables.
    std::vector<z3::goal> goal_chain;
};

// Apply tactic t to expr e. On success with exactly one subgoal, append the
// subgoal to goal_chain and update e. On timeout/failure/split, leave both
// unchanged (or update e without recording an MC entry).
static void run_tactic(z3::context& zctx, z3::expr& e,
                       std::vector<z3::goal>& goal_chain,
                       const z3::tactic& t, int timeout_ms) {
    try {
        z3::goal g(zctx);
        g.add(e);
        z3::apply_result res = z3::try_for(t, timeout_ms)(g);
        z3::expr_vector all(zctx);
        for (unsigned i = 0; i < res.size(); ++i)
            for (unsigned j = 0; j < res[i].size(); ++j)
                all.push_back(res[i][j]);
        e = z3::mk_and(all);
        // Only record the MC entry when there is exactly one subgoal; if the
        // tactic produced a case split we cannot chain convert_model cleanly.
        if (res.size() == 1)
            goal_chain.push_back(res[0]);
    } catch (const z3::exception&) {}
}

static PreprocessResult preprocess_plain(const Ctx& ctx, const smt::Term& formula,
                                         int timeout_ms) {
    auto* z3s = dynamic_cast<smt::Z3Solver*>(ctx.solver.get());
    z3::context& zctx = *z3s->get_z3_context();
    z3::expr e = dynamic_cast<smt::Z3Term*>(formula.get())->get_z3_expr();

    std::vector<z3::goal> goal_chain;
    for (const char* name : {"simplify", "propagate-values", "solve-eqs", "simplify"})
        run_tactic(zctx, e, goal_chain, z3::tactic(zctx, name), timeout_ms);

    return {std::make_shared<smt::Z3Term>(e, zctx), std::move(goal_chain)};
}

static PreprocessResult preprocess_aggressive(const Ctx& ctx, const smt::Term& formula,
                                              int level, int timeout_ms) {
    auto* z3s = dynamic_cast<smt::Z3Solver*>(ctx.solver.get());
    z3::context& zctx = *z3s->get_z3_context();
    z3::expr e = dynamic_cast<smt::Z3Term*>(formula.get())->get_z3_expr();

    std::vector<z3::goal> goal_chain;
    auto run = [&](z3::tactic t) { run_tactic(zctx, e, goal_chain, t, timeout_ms); };

    using P = z3::params;
    auto p_simplify = [&](bool hoist) {
        P p(zctx);
        p.set("arith_lhs", true);
        p.set("hoist_mul", hoist);
        p.set("som", true);
        run(z3::with(z3::tactic(zctx, "simplify"), p));
    };
    auto p_propagate_values = [&]() {
        P p(zctx);
        p.set("local_ctx", true);
        p.set("arith_lhs", true);
        p.set("rewrite_patterns", true);
        run(z3::with(z3::tactic(zctx, "propagate-values"), p));
    };
    auto p_solve_eqs = [&]() {
        P p(zctx);
        p.set("context_solve", true);
        run(z3::with(z3::tactic(zctx, "solve-eqs"), p));
    };

    p_simplify(true);
    p_propagate_values();
    run(z3::tactic(zctx, "propagate-ineqs"));
    // normalize-bounds is omitted: it introduces fresh k! variables that appear
    // in nonlinear products, creating new NIA terms absent from the original
    // formula. The model converter then reconstructs wrong values for the
    // original variables.
    p_solve_eqs();
    p_simplify(true);
    run(z3::tactic(zctx, "ctx-simplify"));
    p_simplify(false);

    if (level >= 2) {
        run(z3::tactic(zctx, "ctx-solver-simplify"));
        p_simplify(false);
    }

    return {std::make_shared<smt::Z3Term>(e, zctx), std::move(goal_chain)};
}
#endif

// ── Parse SMT2 input ──────────────────────────────────────────────────────────
static smt::Term parse_input(const Ctx& ctx, const std::string& filename) {
#ifdef BACKEND_Z3
    auto* z3s = dynamic_cast<smt::Z3Solver*>(ctx.solver.get());
    if (!z3s) {
        std::fprintf(stderr, "parse_input: expected Z3Solver\n");
        std::exit(1);
    }
    z3::context& zctx = *z3s->get_z3_context();
    z3::expr_vector assertions(zctx);
    if (filename == "-") {
        std::string content((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
        assertions = zctx.parse_string(content.c_str());
    } else {
        assertions = zctx.parse_file(filename.c_str());
    }
    smt::TermVec terms;
    for (unsigned i = 0; i < assertions.size(); ++i)
        terms.push_back(std::make_shared<smt::Z3Term>(assertions[i], zctx));
    return mk_and(ctx, terms);
#else
    std::fprintf(stderr, "parse_input: only Z3 backend supports parsing\n");
    std::exit(1);
#endif
}

// ── Model printing ────────────────────────────────────────────────────────────
#ifdef BACKEND_Z3
// Apply the preprocessing MC chain in reverse to reconstruct values for
// variables eliminated by tactics like solve-eqs, then print all orig_syms.
static void print_model(const Ctx& ctx, const smt::UnorderedTermSet& orig_syms,
                        const std::vector<z3::goal>& goal_chain) {
    std::printf(";; model-start\n");
    auto* z3s = dynamic_cast<smt::Z3Solver*>(ctx.solver.get());
    z3::model m = z3s->get_z3_solver()->get_model();
    for (int i = static_cast<int>(goal_chain.size()) - 1; i >= 0; --i)
        m = goal_chain[i].convert_model(m);
    for (auto& sym : orig_syms) {
        auto* z3t = dynamic_cast<smt::Z3Term*>(sym.get());
        z3::expr val = m.eval(z3t->get_z3_expr(), /*model_completion=*/true);
        if (val.is_numeral())
            std::printf("(define-fun %s () Int %s)\n",
                        sym->to_string().c_str(), val.to_string().c_str());
    }
    std::printf(";; model-end\n");
}
#else
static void print_model(const Ctx& /*ctx*/, const LiaAbstraction& abstr,
                        const smt::UnorderedTermSet& orig_syms) {
    std::printf(";; model-start\n");
    for (auto& sym : orig_syms) {
        std::optional<smt::Term> val = abstr.get_value(sym);
        if (!val)
            continue;
        std::printf("(define-fun %s () Int %s)\n", sym->to_string().c_str(),
                    (*val)->to_string().c_str());
    }
    std::printf(";; model-end\n");
}
#endif

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    std::string filename;
    Options opts = parse_args(argc, argv, filename);

    g_print_stats = opts.print_stats;
    g_brief_stats = opts.brief_stats;
    STATS.model_fix = opts.model_fix || opts.model_fix2;
    g_start_time = std::chrono::duration<double>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
    opts.start_time = g_start_time;

    std::signal(SIGTERM, handle_signal);
    std::signal(SIGINT, handle_signal);

    if (opts.timeout > 0) {
        std::signal(SIGALRM, handle_signal);
        // Use setitimer for wall-clock timeout.
        // TODO: On non-POSIX systems, use a timer thread instead.
#ifdef SIGALRM
        struct itimerval itv;
        itv.it_value.tv_sec = static_cast<long>(opts.timeout);
        itv.it_value.tv_usec =
            static_cast<long>((opts.timeout - itv.it_value.tv_sec) * 1e6);
        itv.it_interval = {0, 0};
        setitimer(ITIMER_REAL, &itv, nullptr);
#endif
    }

    // ── Create solver and context ─────────────────────────────────────────────
    print_backend_version(opts.backend);
    smt::SmtSolver raw_solver = create_solver(opts.backend);
    Ctx ctx(raw_solver);

    // ── Parse ─────────────────────────────────────────────────────────────────
    STATS.begin_phase(STATS.parse_time);
    smt::Term formula;
    try {
        formula = parse_input(ctx, filename);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Parse error: %s\n", e.what());
        return 1;
    }
    STATS.end_phase();

    // Collect symbols before preprocessing so the MC can reconstruct values
    // for variables that tactics like solve-eqs eliminate.
    smt::UnorderedTermSet orig_syms;
    if (opts.print_model)
        for (auto& v : get_vars(formula))
            orig_syms.insert(v);

#ifdef BACKEND_Z3
    std::vector<z3::goal> goal_chain;
    if (opts.preprocess_aggressive > 0) {
        STATS.begin_phase(STATS.parse_time);
        auto pr = preprocess_aggressive(ctx, formula, opts.preprocess_aggressive,
                                        opts.preprocess_aggressive_timeout);
        formula = std::move(pr.formula);
        goal_chain = std::move(pr.goal_chain);
        STATS.end_phase();
    } else if (opts.preprocess) {
        STATS.begin_phase(STATS.parse_time);
        auto pr = preprocess_plain(ctx, formula, opts.preprocess_aggressive_timeout);
        formula = std::move(pr.formula);
        goal_chain = std::move(pr.goal_chain);
        STATS.end_phase();
    }
#endif

    // ── Solve ─────────────────────────────────────────────────────────────────
    std::optional<bool> res;
    try {
        STATS.begin_phase(STATS.init_time);
        QfSolver solver(ctx, opts, formula);
        STATS.end_phase();
        res = solver.solve();

        if (opts.timeout > 0) {
#ifdef SIGALRM
            struct itimerval off = {{0, 0}, {0, 0}};
            setitimer(ITIMER_REAL, &off, nullptr);
#endif
        }

        double now = std::chrono::duration<double>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
        STATS.total_time.value += now - g_start_time;
        if (opts.brief_stats) {
            STATS.brief_prn();
            std::printf("\n");
        } else if (opts.print_stats) {
            STATS.prn();
            std::printf("\n");
        }

        if (!res) {
            std::printf("unknown\n");
        } else if (*res) {
            if (opts.print_model)
#ifdef BACKEND_Z3
                print_model(ctx, orig_syms, goal_chain);
#else
                print_model(ctx, solver.abstraction(), orig_syms);
#endif
            std::printf("sat\n");
        } else {
            std::printf("unsat\n");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
