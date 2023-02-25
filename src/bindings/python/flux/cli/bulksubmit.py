##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import itertools
import random
import sys

from flux.cli import base


class BulkSubmitCmd(base.SubmitBulkCmd):
    """
    BulkSubmitCmd is like xargs for job submission. It takes a series of
    inputs on stdin (or the cmdline separated by :::), and substitutes them
    into the initial arguments, e.g::

       $ echo 1 2 3 | flux bulksubmit echo {}

    """

    def __init__(self, prog, usage=None, description=None):
        super().__init__(prog, usage, description)
        self.parser.add_argument(
            "--shuffle",
            action="store_true",
            help="Shuffle list of commands before submission",
        )
        self.parser.add_argument(
            "--sep",
            type=str,
            metavar="STRING",
            default="\n",
            help="Set the input argument separator. To split on whitespace, "
            "use --sep=none. The default is newline.",
        )
        self.parser.add_argument(
            "--define",
            action="append",
            type=lambda kv: kv.split("="),
            dest="methods",
            default=[],
            help="Define a named method for transforming any input, "
            "accessible via e.g. '{0.NAME}'. (local variable 'x' "
            "will contain the input string to be transformed)",
            metavar="NAME=CODE",
        )
        self.parser.add_argument(
            "command",
            nargs=argparse.REMAINDER,
            help="Job command and initial arguments",
        )

    @staticmethod
    def input_file(filep, sep):
        """Read set of inputs from file object filep, using separator sep"""
        return list(filter(None, filep.read().split(sep)))

    @staticmethod
    def split_before(iterable, pred):
        """
        Like more_itertools.split_before, but if predicate returns
        True on first element, then return an empty list
        """
        buf = []
        for item in iter(iterable):
            if pred(item):
                yield buf
                buf = []
            buf.append(item)
        yield buf

    def split_command_inputs(self, command, sep="\n", delim=":::"):
        """Generate a list of inputs from command list

        Splits the command list on the input delimiter ``delim``,
        and returns 3 lists:

            - the initial command list (everything before the first delim)
            - a list of normal "input lists"
            - a list of "linked" input lists, (delim + "+")

        Special case delimiter values are handled here,
        e.g. ":::+" and ::::".

        """
        links = []

        #  Split command array into commands and inputs on ":::":
        command, *input_lists = self.split_before(
            command, lambda x: x.startswith(delim)
        )

        #  Remove ':::' separators from each input, allowing GNU parallel
        #   like alternate separators e.g. '::::' and ':::+':
        #
        for i, lst in enumerate(input_lists):
            first = lst.pop(0)
            if first == delim:
                #  Normal input
                continue
            if first == delim + ":":
                #
                #  Read input from file
                if len(lst) > 1:
                    raise ValueError("Multiple args not allowed after ::::")
                if lst[0] == "-":
                    input_lists[i] = self.input_file(sys.stdin, sep)
                else:
                    with open(lst[0]) as filep:
                        input_lists[i] = self.input_file(filep, sep)
            if first in (delim + "+", delim + ":+"):
                #
                #  "Link" input to previous, similar to GNU parallel:
                #  Clear list so this entry can be removed below after
                #   iteration is complete:
                links.append({"index": i, "list": lst.copy()})
                lst.clear()

        #  Remove empty lists (which are now links)
        input_lists = [lst for lst in input_lists if lst]

        return command, input_lists, links

    def create_commands(self, args):
        """Create bulksubmit commands list"""

        #  Expand any escape sequences in args.sep, and replace "none"
        #   with literal None:
        sep = bytes(args.sep, "utf-8").decode("unicode_escape")
        if sep.lower() == "none":
            sep = None

        #  Ensure any provided methods can compile
        args.methods = {
            name: compile(code, name, "eval")
            for name, code in dict(args.methods).items()
        }

        #  Split command into command template and input lists + links:
        args.command, input_list, links = self.split_command_inputs(
            args.command, sep, delim=":::"
        )

        #  If no command provided then the default is "{}"
        if not args.command:
            args.command = ["{}"]

        #  If no inputs on commandline, read from stdin:
        if not input_list:
            input_list = [self.input_file(sys.stdin, sep)]

        #  Take the product of all inputs in input_list
        inputs = [list(x) for x in list(itertools.product(*input_list))]

        #  Now cycle over linked inputs and insert them in result:
        for link in links:
            cycle = itertools.cycle(link["list"])
            for lst in inputs:
                lst.insert(link["index"], next(cycle))

        #  For each set of generated input lists, append a command
        #   to run. Keep a sequence counter so that {seq} can be used
        #   in the format expansion.
        return [
            base.Xcmd(args, inp, seq=i, seq1=i + 1, cc="{cc}")
            for i, inp in enumerate(inputs)
        ]

    def main(self, args):
        if not args.command:
            args.command = ["{}"]

        #  Create one "command" to be run for each combination of
        #   "inputs" on stdin or the command line:
        #
        commands = self.create_commands(args)

        if args.shuffle:
            random.shuffle(commands)

        #  Calculate total number of commands for use with progress:
        total = 0
        for xargs in commands:
            total += len(self.cc_list(xargs))

        if total == 0:
            raise ValueError("no jobs provided for bulk submission")
        #  Initialize progress bar if requested:
        if args.progress:
            if not args.dry_run:
                self.progress_start(args, total)
            else:
                print(f"bulksubmit: submitting a total of {total} jobs")

        #  Loop through commands and asynchronously submit them:
        for xargs in commands:
            if args.verbose or args.dry_run:
                print(f"bulksubmit: submit {xargs}")
            if not args.dry_run:
                self.submit_async_with_cc(xargs)

        if not args.dry_run:
            self.run_and_exit()
