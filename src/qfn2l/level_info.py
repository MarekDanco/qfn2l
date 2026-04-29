#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bookkeeping for quantification levels of constants in the formula."""

# Created on:  Mon Dec 8 10:38:13 CET 2025
# Copyright (C) 2025, Mikolas Janota

from z3 import BoolRef

from prefix import QLev
from utils import GetLevel


class FormulaInfo:
    def __init__(self, prefix: list[QLev], body: BoolRef):
        self.prefix = prefix
        self.body: BoolRef = body
        self._const2lev = {
            v: lev for lev, qlev in enumerate(self.prefix) for v in qlev.vars()
        }
        self._get_level = GetLevel(
            self._const2lev, self.body
        )  # make sure that all the terms are already covered

    def add_const(self, c, lev):
        self._const2lev[c] = lev

    def get_level(self, term):
        return self._get_level(term)

    def get_terms(self):
        return self._get_level.terms()
