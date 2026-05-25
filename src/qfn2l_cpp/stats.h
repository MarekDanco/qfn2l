#pragma once
#include <chrono>
#include <cstdio>
#include <vector>

struct TimedStat {
    const char* name;
    double value = 0.0;
};

struct CountStat {
    const char* name;
    long value = 0;
    CountStat& operator+=(long n) {
        value += n;
        return *this;
    }
    bool operator>=(long n) const { return value >= n; }
    bool operator<(long n) const { return value < n; }
};

// Global solver statistics.
struct Stats {
    CountStat its{"iteration"};
    CountStat pures{"pures"};
    CountStat mul_axioms{"mul_axioms"};
    CountStat mod_axioms{"mod_axioms"};
    CountStat div_axioms{"div_axioms"};
    CountStat skipped_pures{"skipped_pures"};
    CountStat liacalls{"lia_calls"};
    CountStat model_fix_attempts{"model_fix_attempts"};
    CountStat model_fix_successes{"model_fix_successes"};

    TimedStat liatime{"lia_time"};
    TimedStat parse_time{"parse_time"};
    TimedStat nnf_time{"nnf_time"};
    TimedStat simplify_time{"simplify_time"};
    TimedStat propagate_time{"propagate_time"};
    TimedStat makedefs_time{"makedefs_time"};
    TimedStat init_time{"init_time"};
    TimedStat set_level_time{"set_level_time"};
    TimedStat check_nia_time{"check_nia_time"};
    TimedStat model_fix_time{"model_fix_time"};
    TimedStat solve_time{"solve_time"};
    TimedStat complete_model_time{"complete_model_time"};
    TimedStat total_time{"total_time"};

    bool model_fix = false;

    using Clock = std::chrono::steady_clock;
    using TP = std::chrono::time_point<Clock>;

    struct Phase {
        TimedStat* stat;
        TP start;
    };
    std::vector<Phase> phase_stack;

    void begin_phase(TimedStat& s);
    void end_phase();
    void commit_phases(); // called from signal handler

    void prn() const;
    void brief_prn() const;
};

extern Stats STATS;

// RAII helper
struct ScopedPhase {
    explicit ScopedPhase(TimedStat& s) { STATS.begin_phase(s); }
    ~ScopedPhase() { STATS.end_phase(); }
};
