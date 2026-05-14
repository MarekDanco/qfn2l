#include "qf_solver.h"
#include "stats.h"
#include "tagged_logging.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <unistd.h>

// ── Signal handling ───────────────────────────────────────────────────────────
static volatile sig_atomic_t g_shutdown = 0;
static bool g_print_stats = false;
static bool g_brief_stats = false;
static double g_start_time = 0.0;

static void handle_signal(int) {
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
        "  --bounds              Heuristic bounds on LIA solver\n"
        "  --zeros               Try setting mul pures to 0\n"
        "  --static              Add static axioms for div/mod\n"
        "  --seed N              Random seed (default 7)\n"
        "  --timeout N           Wall-clock timeout in seconds (-1 = none)\n"
        "  --heur-timeout N      Heuristic LIA timeout in ms (default 3000)\n"
        "  --print-model         Print SAT model as define-fun lines\n"
        "  --stats               Print full stats on exit\n"
        "  --brief-stats         Print brief stats on exit\n"
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
        else if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg[0] != '-')
            filename = arg;
        else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            print_usage(argv[0]);
            std::exit(1);
        }
    }
    return opts;
}

// ── Parse SMT2 input ──────────────────────────────────────────────────────────
static Term parse_input(Ctx& ctx, const std::string& filename) {
    z3::expr_vector assertions(ctx.zctx);
    if (filename == "-") {
        std::string content((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
        assertions = ctx.zctx.parse_string(content.c_str());
    } else {
        assertions = ctx.zctx.parse_file(filename.c_str());
    }
    TermVec terms;
    for (unsigned i = 0; i < assertions.size(); ++i)
        terms.push_back(assertions[i]);
    return mk_and(ctx, terms);
}

// ── Model printing ────────────────────────────────────────────────────────────
static void print_model(const LiaAbstraction& abstr, const TermSet& orig_syms) {
    std::printf(";; model-start\n");
    for (auto& sym : orig_syms) {
        std::optional<Term> val = abstr.get_value(sym);
        if (!val)
            continue;
        std::printf("(define-fun %s () Int %s)\n", sym.to_string().c_str(),
                    val->to_string().c_str());
    }
    std::printf(";; model-end\n");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    std::string filename;
    Options opts = parse_args(argc, argv, filename);

    g_print_stats = opts.print_stats;
    g_brief_stats = opts.brief_stats;
    g_start_time = std::chrono::duration<double>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();
    opts.start_time = g_start_time;

    std::signal(SIGTERM, handle_signal);
    std::signal(SIGINT, handle_signal);

    if (opts.timeout > 0) {
        std::signal(SIGALRM, handle_signal);
#ifdef SIGALRM
        struct itimerval itv;
        itv.it_value.tv_sec = static_cast<long>(opts.timeout);
        itv.it_value.tv_usec =
            static_cast<long>((opts.timeout - itv.it_value.tv_sec) * 1e6);
        itv.it_interval = {0, 0};
        setitimer(ITIMER_REAL, &itv, nullptr);
#endif
    }

    Ctx ctx;

    // ── Parse ─────────────────────────────────────────────────────────────────
    STATS.begin_phase(STATS.parse_time);
    Term formula(ctx.zctx);
    try {
        formula = parse_input(ctx, filename);
    } catch (const z3::exception& e) {
        std::fprintf(stderr, "Parse error: %s\n", e.msg());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Parse error: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "Parse error: unknown exception from Z3\n");
        return 1;
    }
    STATS.end_phase();

    // Collect original symbols for model printing.
    TermSet orig_syms;
    if (opts.print_model)
        for (auto& v : get_vars(formula))
            orig_syms.insert(v);

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
                print_model(solver.abstraction(), orig_syms);
            std::printf("sat\n");
        } else {
            std::printf("unsat\n");
        }
    } catch (const z3::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.msg());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "Error: unknown exception from Z3\n");
        return 1;
    }

    return 0;
}
