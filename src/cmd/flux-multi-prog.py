###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import logging
import os
import re
import shlex
import sys

import flux
from flux.idset import IDset
from flux.util import CLIMain


class MultiProgLine:
    """Class representing a single "multi-prog" config line"""

    def __init__(self, value, lineno=-1):
        self.ranks = IDset()
        self.all = False
        self.args = []
        self.lineno = lineno
        lexer = shlex.shlex(value, posix=True, punctuation_chars=True)
        lexer.whitespace_split = True
        lexer.escapedquotes = "\"'"
        try:
            args = list(lexer)
        except ValueError as exc:
            raise ValueError(f"line {lineno}: '{value.rstrip()}': {exc}") from None
        if not args:
            return

        targets = args.pop(0)
        if targets == "*":
            self.all = True
        else:
            try:
                self.ranks = IDset(targets)
            except ValueError:
                raise ValueError(f"line {lineno}: invalid idset: {targets}") from None

        self.args = args

    def get_args(self, rank):
        """Return the arguments list with %t and %o substituted for `rank`"""

        result = []
        index = 0
        if not self.all:
            index = self.ranks.expand().index(rank)
        sub = {"%t": str(rank), "%o": str(index)}
        for arg in self.args:
            result.append(re.sub(r"(%t)|(%o)", lambda x: sub[x.group(0)], arg))
        return result

    def __bool__(self):
        return bool(self.args)


class MultiProg:
    """Class representing an entire "multi-prog" config file"""

    def __init__(self, inputfile):
        self.fp = inputfile
        self.lines = []
        self.fallthru = None
        lineno = 0
        for line in self.fp:
            lineno += 1
            try:
                mpline = MultiProgLine(line, lineno)
            except ValueError as exc:
                raise ValueError(f"{self.fp.name}: {exc}") from None
            if mpline:
                if mpline.all:
                    self.fallthru = mpline
                else:
                    self.lines.append(mpline)

    def find(self, rank):
        """Return line matching 'rank' in the current config"""
        for line in self.lines:
            if rank in line.ranks:
                return line
        if self.fallthru is not None:
            return self.fallthru
        raise ValueError(f"{self.fp.name}: No matching line for rank {rank}")

    def exec(self, rank, dry_run=False):
        """Exec configured command line arguments for a task rank"""
        args = self.find(rank).get_args(rank)
        if dry_run:
            args = " ".join(shlex.quote(arg) for arg in args)
            print(f"{rank}: {args}")
        else:
            os.execvp(args[0], args)


def parse_args():
    description = """
    Run a parallel program with a different executable and arguments for each task
    """
    parser = argparse.ArgumentParser(
        prog="flux-multi-prog",
        usage="flux multi-prog [OPTIONS] CONFIG",
        description=description,
        formatter_class=flux.util.help_formatter(),
    )
    parser.add_argument(
        "-n",
        "--dry-run",
        type=IDset,
        metavar="IDS",
        help="Do not run anything. Instead, print what would be run for"
        + " each rank in IDS",
    )
    parser.add_argument(
        "conf", metavar="CONFIG", type=str, help="multi-prog configuration file"
    )
    return parser.parse_args()


LOGGER = logging.getLogger("flux-multi-prog")


@CLIMain(LOGGER)
def main():

    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")

    args = parse_args()

    with open(args.conf) as infile:
        mp = MultiProg(infile)

    if args.dry_run:
        for rank in args.dry_run:
            mp.exec(rank, dry_run=True)
        sys.exit(0)

    try:
        rank = int(os.getenv("FLUX_TASK_RANK"))
    except TypeError:
        raise ValueError("FLUX_TASK_RANK not found or invalid")

    mp.exec(rank)
