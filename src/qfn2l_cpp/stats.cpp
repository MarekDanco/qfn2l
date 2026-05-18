#include "stats.h"
#include <cstdio>

Stats STATS;

void Stats::begin_phase(TimedStat& s) { phase_stack.push_back({&s, Clock::now()}); }

void Stats::end_phase() {
    auto& ph = phase_stack.back();
    auto elapsed = std::chrono::duration<double>(Clock::now() - ph.start).count();
    ph.stat->value += elapsed;
    phase_stack.pop_back();
}

void Stats::commit_phases() {
    auto now = Clock::now();
    for (auto& ph : phase_stack) {
        auto elapsed = std::chrono::duration<double>(now - ph.start).count();
        ph.stat->value += elapsed;
    }
}

void Stats::prn() const {
    auto prn_count = [](const CountStat& s) {
        std::printf("%-24s %ld\n", s.name, s.value);
    };
    auto prn_timed = [](const TimedStat& s) {
        std::printf("%-24s %.2f\n", s.name, s.value);
    };
    prn_count(its);
    prn_count(pures);
    prn_count(mul_axioms);
    prn_count(mod_axioms);
    prn_count(div_axioms);
    prn_count(liacalls);
    prn_timed(liatime);
    prn_timed(parse_time);
    prn_timed(nnf_time);
    prn_timed(simplify_time);
    prn_timed(propagate_time);
    prn_timed(makedefs_time);
    prn_timed(init_time);
    prn_timed(set_level_time);
    prn_timed(check_nia_time);
    prn_timed(solve_time);
    prn_timed(complete_model_time);
    prn_timed(total_time);
}

void Stats::brief_prn() const {
    if (!phase_stack.empty())
        std::printf("terminated in: %s\n", phase_stack.back().stat->name);

    // Find longest non-container timed stat with value > 0
    std::vector<const TimedStat*> timed = {
        &liatime,        &parse_time,     &nnf_time,
        &simplify_time,  &propagate_time, &makedefs_time,
        &set_level_time, &check_nia_time, &complete_model_time};
    const TimedStat* longest = nullptr;
    for (auto* s : timed) {
        if (s->value > 0 && (!longest || s->value > longest->value))
            longest = s;
    }
    if (longest)
        std::printf("longest phase: %s (%.2fs)\n", longest->name, longest->value);

    std::printf("iteration:  %ld\n", its.value);
    std::printf("pures:      %ld\n", pures.value);
    std::printf("mul_axioms: %ld\n", mul_axioms.value);
    std::printf("div_axioms: %ld\n", div_axioms.value);
    std::printf("mod_axioms: %ld\n", mod_axioms.value);
}
