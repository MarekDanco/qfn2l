#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verbosity-gated tagged logging. Each module registers a tag and calls mk_logfn."""

# Created on:  Mon Dec  8 10:55:12 CET 2025
# Copyright (C) 2025, Mikolas Janota

VERBOSITY_LEVELS = {}


def log(tag, lev, *args, **kwargs):
    """Like print, but only if lev > VERBOSE."""
    if lev > VERBOSITY_LEVELS.get(tag, -1):
        return
    print(f"[{tag}]", end=" ")
    print(*args, **kwargs, flush=True)


def mk_logfn(tag):
    def tag_log(lev, *args, **kwargs):
        log(tag, lev, *args, **kwargs)

    return tag_log
