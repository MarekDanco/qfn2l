#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Statistics tracking for the solver."""

import time
import typing


class Stat:
    """A single trackable statistic that behaves like a number."""

    def __init__(self, name: str, initial: typing.Any = 0, fmt: str = ""):
        self.name = name
        self.value = initial
        self.initial = initial
        self.fmt = fmt

    def reset(self):
        self.value = self.initial

    def __iadd__(self, other):
        self.value += other
        return self

    def __str__(self):
        return f"{self.value:{self.fmt}}" if self.fmt else str(self.value)

    def __repr__(self):
        return str(self.value)

    def __gt__(self, other):
        return self.value > other

    def __ge__(self, other):
        return self.value >= other

    def __lt__(self, other):
        return self.value < other

    def __le__(self, other):
        return self.value <= other

    def __eq__(self, other):
        return self.value == other


class Stats:
    """Statistics tracking for the solver."""

    def __init__(self) -> None:
        self._phase_stack: list[tuple["Stat", float]] = []
        self.its = Stat("iteration", 0)
        self.pures = Stat("pures", 0)
        self.mul_axioms = Stat("mul_axioms", 0)
        self.mod_axioms = Stat("mod_axioms", 0)
        self.div_axioms = Stat("div_axioms", 0)
        self.liacalls = Stat("lia_calls", 0)
        self.liatime = Stat("lia_time", 0.0, ".2f")
        self.parse_time = Stat("parse_time", 0.0, ".2f")
        self.nnf_time = Stat("nnf_time", 0.0, ".2f")
        self.simplify_time = Stat("simplify_time", 0.0, ".2f")
        self.propagate_time = Stat("propagate_time", 0.0, ".2f")
        self.makedefs_time = Stat("makedefs_time", 0.0, ".2f")
        self.init_time = Stat("init_time", 0.0, ".2f")
        self.set_level_time = Stat("set_level_time", 0.0, ".2f")
        self.check_nia_time = Stat("check_nia_time", 0.0, ".2f")
        self.solve_time = Stat("solve_time", 0.0, ".2f")
        self.complete_model_time = Stat("complete_model_time", 0.0, ".2f")
        self.total_time = Stat("total_time", 0.0, ".2f")
        self._all = [
            self.its,
            self.pures,
            self.mul_axioms,
            self.mod_axioms,
            self.div_axioms,
            self.liacalls,
            self.liatime,
            self.parse_time,
            self.nnf_time,
            self.simplify_time,
            self.propagate_time,
            self.makedefs_time,
            self.init_time,
            self.set_level_time,
            self.check_nia_time,
            self.solve_time,
            self.complete_model_time,
            self.total_time,
        ]
        self._containers = [self.init_time, self.solve_time, self.total_time]

    def reset(self) -> None:
        for stat in self._all:
            stat.reset()

    def begin_phase(self, stat: "Stat") -> None:
        self._phase_stack.append((stat, time.perf_counter()))

    def end_phase(self) -> None:
        stat, start = self._phase_stack.pop()
        stat += time.perf_counter() - start

    def commit_phases(self) -> None:
        """Commit all in-progress phases; called from the alarm handler."""
        now = time.perf_counter()
        for stat, start in self._phase_stack:
            stat += now - start

    def prn(self) -> None:
        width = max(len(s.name) for s in self._all) + 1
        for stat in self._all:
            print(f"{stat.name + ':':<{width}} {stat}")

    def brief_prn(self) -> None:
        if self._phase_stack:
            print(f"terminated in: {self._phase_stack[-1][0].name}")
        timed = [
            s
            for s in self._all
            if s.fmt == ".2f" and s not in self._containers and s.value > 0
        ]
        if timed:
            longest = max(timed, key=lambda s: s.value)
            print(f"longest phase: {longest.name} ({longest}s)")
        print(f"{self.its.name}: {self.its}")
        print(f"{self.pures.name}: {self.pures}")


STATS = Stats()


def timed_check(solver, *args):
    """Wrapper for z3 solver check that tracks time in STATS.liatime."""
    STATS.begin_phase(STATS.liatime)
    result = solver.check(*args)
    STATS.end_phase()
    STATS.liacalls += 1
    return result
