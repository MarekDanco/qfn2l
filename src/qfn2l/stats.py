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
        self.its = Stat("iteration", 0)
        self.bts = Stat("backtracks", 0)
        self.liacalls = Stat("lia_calls", 0)
        self.liatime = Stat("lia_time", 0.0, ".2f")
        self.parse_time = Stat("parse_time", 0.0, ".2f")
        self.init_time = Stat("init_time", 0.0, ".2f")
        self.set_level_time = Stat("set_level_time", 0.0, ".2f")
        self.check_nia_time = Stat("check_nia_time", 0.0, ".2f")
        self.solve_time = Stat("solve_time", 0.0, ".2f")
        self.complete_model_time = Stat("complete_model_time", 0.0, ".2f")
        self.mk_ext_assign_time = Stat("mk_ext_assign_time", 0.0, ".2f")
        self.total_time = Stat("total_time", 0.0, ".2f")
        self._all = [
            self.its,
            self.bts,
            self.liacalls,
            self.liatime,
            self.parse_time,
            self.init_time,
            self.set_level_time,
            self.check_nia_time,
            self.solve_time,
            self.complete_model_time,
            self.mk_ext_assign_time,
            self.total_time,
        ]

    def reset(self) -> None:
        for stat in self._all:
            stat.reset()

    def prn(self) -> None:
        width = max(len(s.name) for s in self._all) + 1
        for stat in self._all:
            print(f"{stat.name + ':':<{width}} {stat}")


STATS = Stats()


def timed_check(solver, *args):
    """Wrapper for z3 solver check that tracks time in STATS.liatime."""
    start = time.perf_counter()
    result = solver.check(*args)
    STATS.liatime += time.perf_counter() - start
    STATS.liacalls += 1
    return result
