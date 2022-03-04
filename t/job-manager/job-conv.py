##############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import sys
import argparse

from flux.core.inner import ffi, raw


def statetostr(args):
    fmt = "L"
    if args.single:
        fmt = "S"
    if not args.states:
        args.states = [line.strip() for line in sys.stdin]

    for state in args.states:
        print(raw.flux_job_statetostr(int(state), fmt).decode("utf-8"))


def strtostate(args):
    state = ffi.new("flux_job_state_t [1]")
    if not args.strings:
        args.strings = [line.strip() for line in sys.stdin]

    for s in args.strings:
        try:
            raw.flux_job_strtostate(s, state)
        except Exception:
            print(f"invalid string {s}")
            sys.exit(1)
        print(int(state[0]))


def resulttostr(args):
    if not args.results:
        args.results = [line.strip() for line in sys.stdin]

    for result in args.results:
        print(raw.flux_job_resulttostr(int(result), args.abbrev).decode("utf-8"))


def strtoresult(args):
    result = ffi.new("flux_job_result_t [1]")
    if not args.strings:
        args.strings = [line.strip() for line in sys.stdin]

    for s in args.strings:
        try:
            raw.flux_job_strtoresult(s, result)
        except Exception:
            print(f"invalid string {s}")
            sys.exit(1)
        print(int(result[0]))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="job-conv")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    statetostr_parser = subparsers.add_parser("statetostr")
    statetostr_parser.add_argument(
        "-s",
        "--single",
        action="store_true",
        help="Output single string abbreviation",
    )
    statetostr_parser.add_argument(
        "states",
        nargs="*",
        help="List of states to convert",
    )
    statetostr_parser.set_defaults(func=statetostr)

    strtostate_parser = subparsers.add_parser("strtostate")
    strtostate_parser.add_argument(
        "strings",
        nargs="*",
        help="List of strings to convert",
    )
    strtostate_parser.set_defaults(func=strtostate)

    resulttostr_parser = subparsers.add_parser("resulttostr")
    resulttostr_parser.add_argument(
        "-a",
        "--abbrev",
        action="store_true",
        help="Output abbreviated result string",
    )
    resulttostr_parser.add_argument(
        "results",
        nargs="*",
        help="List of results to convert",
    )
    resulttostr_parser.set_defaults(func=resulttostr)

    strtoresult_parser = subparsers.add_parser("strtoresult")
    strtoresult_parser.add_argument(
        "strings",
        nargs="*",
        help="List of strings to convert",
    )
    strtoresult_parser.set_defaults(func=strtoresult)

    args = parser.parse_args()
    args.func(args)

# vi: ts=4 sw=4 expandtab
