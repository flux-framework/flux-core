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
import logging
import sys

import flux
import flux.job
from flux.cli import base
from flux.job.directives import DirectiveParser

LOGGER = logging.getLogger("flux-batch")


class BatchCmd(base.MiniCmd):
    def __init__(self, prog, usage=None, description=None):
        super().__init__(prog, usage, description)
        self.parser.add_argument(
            "--wrap",
            action="store_true",
            help="Wrap arguments or stdin in a /bin/sh script",
        )
        base.add_batch_alloc_args(self.parser)
        self.parser.add_argument(
            "SCRIPT",
            nargs=argparse.REMAINDER,
            help="Batch script and arguments to submit",
        )

    def parse_directive_args(self, name, batchscript):
        """
        Parse any directives in batchscript.directives, then apply
        command line arguments in self.argv. This allows command line
        to override file directives
        """
        args = None
        for item in batchscript.directives:
            try:
                if item.action == "SETARGS":
                    args = self.parser.parse_args(item.args, namespace=args)
            except SystemExit:
                #  Argparse exits on error. Give the user a clue
                #  about which line failed in the source file:
                LOGGER.error(f"argument parsing failed at {name} line {item.lineno}")
                sys.exit(2)
        args = self.parser.parse_args(self.argv, namespace=args)
        return batchscript.script, args

    def process_script(self, args):
        """
        Process a batch script that may contain RFC 36 directives.
        Returns the ingested script and new argparse args Namespace.
        """
        if args.SCRIPT:
            if args.wrap:
                #  Return script which will be wrapped by caller
                return " ".join(args.SCRIPT) + "\n", args

            # O/w, open script for reading
            name = open_arg = args.SCRIPT[0]
        else:
            name = "stdin"
            open_arg = 0  # when passed to `open`, 0 gives the `stdin` stream
        with open(open_arg, "r", encoding="utf-8") as filep:
            try:
                batchscript = DirectiveParser(filep)
            except UnicodeError:
                raise ValueError(
                    f"{name} does not appear to be a script, "
                    "or failed to encode as utf-8"
                )
            except ValueError as exc:
                raise ValueError(f"{name}: {exc}") from None
        return self.parse_directive_args(name, batchscript)

    def init_jobspec(self, args):
        if args.wrap:
            self.script = f"#!/bin/sh\n{self.script}"

        #  If number of slots not specified, then set it to node count
        #   if set, otherwise raise an error.
        if not args.nslots:
            if not args.nodes:
                raise ValueError("Number of slots to allocate must be specified")
            args.nslots = args.nodes
            args.exclusive = True

        if args.dump:
            args.broker_opts = args.broker_opts or []
            args.broker_opts.append("-Scontent.dump=" + args.dump)

        #  If job name is not explicitly set in args, use the script name
        #   if a script was provided, else the string "batch" to
        #   indicate the script was set on flux batch stdin.
        if args.job_name is None:
            if args.SCRIPT:
                args.job_name = args.SCRIPT[0]
            else:
                args.job_name = "batch"

        jobspec = flux.job.JobspecV1.from_batch_command(
            script=self.script,
            jobname=args.job_name,
            args=args.SCRIPT[1:],
            num_slots=args.nslots,
            cores_per_slot=args.cores_per_slot,
            gpus_per_slot=args.gpus_per_slot,
            num_nodes=args.nodes,
            broker_opts=base.list_split(args.broker_opts),
            exclusive=args.exclusive,
            conf=args.conf.config,
        )

        # Default output is flux-{{jobid}}.out
        # overridden by either --output=none or --output=kvs
        if not args.output:
            jobspec.stdout = "flux-{{id}}.out"
        return jobspec

    def main(self, args):
        #  Save cmdline argv to flux-batch in case it must be reprocessed
        #  after applying directive options.
        #  self.argv is sys.argv without flux-batch:
        self.argv = sys.argv[1:]
        if self.argv and self.argv[0] == "batch":
            self.argv.pop(0)

        #  Process file with possible submission directives, returning
        #  script and new argparse args Namespace as a result.
        #  This must be done before calling self.submit() so that SETARGS
        #  directives are available in jobspec_create():
        self.script, args = self.process_script(args)

        jobid = self.submit(args)
        if not args.quiet:
            print(jobid, file=sys.stdout)
