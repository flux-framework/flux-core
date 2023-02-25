##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
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
import flux.util
from flux.cli.alloc import AllocCmd
from flux.cli.batch import BatchCmd
from flux.cli.bulksubmit import BulkSubmitCmd
from flux.cli.run import RunCmd
from flux.cli.submit import SubmitCmd

LOGGER = logging.getLogger("flux-mini")


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    parser = argparse.ArgumentParser(prog="flux-mini")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # run
    run = RunCmd("flux mini run")
    mini_run_parser_sub = subparsers.add_parser(
        "run",
        parents=[run.get_parser()],
        help="run a job interactively",
        formatter_class=flux.util.help_formatter(),
        add_help=False,
    )
    mini_run_parser_sub.set_defaults(func=run.main)

    # submit
    submit = SubmitCmd("flux mini submit")
    mini_submit_parser_sub = subparsers.add_parser(
        "submit",
        parents=[submit.get_parser()],
        help="enqueue a job",
        formatter_class=flux.util.help_formatter(),
        add_help=False,
    )
    mini_submit_parser_sub.set_defaults(func=submit.main)

    # bulksubmit
    bulksubmit = BulkSubmitCmd("flux mini bulksubmit")
    description = """
    Submit a series of commands given on the command line or on stdin,
    using an interface similar to GNU parallel or xargs.
    Allows jobs to be submitted much faster than calling flux-mini
    submit in a loop. Inputs on the command line are separated from
    each other and the command with the special delimiter ':::'.
    """
    bulksubmit_parser_sub = subparsers.add_parser(
        "bulksubmit",
        parents=[bulksubmit.get_parser()],
        help="enqueue jobs in bulk",
        usage="flux mini bulksubmit [OPTIONS...] COMMAND [ARGS...]",
        description=description,
        formatter_class=flux.util.help_formatter(),
        add_help=False,
    )
    bulksubmit_parser_sub.set_defaults(func=bulksubmit.main)

    # batch
    batch = BatchCmd("flux mini batch")
    description = """
    Submit a batch SCRIPT and ARGS to be run as the initial program of
    a Flux instance.  If no batch script is provided, one will be read
    from stdin.
    """
    batch.parser = subparsers.add_parser(
        "batch",
        parents=[batch.get_parser()],
        help="enqueue a batch script",
        usage="flux mini batch [OPTIONS...] [SCRIPT] [ARGS...]",
        description=description,
        formatter_class=flux.util.help_formatter(),
        add_help=False,
    )
    batch.parser.set_defaults(func=batch.main)

    # alloc
    alloc = AllocCmd("flux mini alloc")
    description = """
    Allocate resources and start a new Flux instance. Once the instance
    has started, attach to it interactively.
    """
    alloc.parser = subparsers.add_parser(
        "alloc",
        parents=[alloc.get_parser()],
        help="allocate a new instance for interactive use",
        usage="flux mini alloc [COMMAND] [ARGS...]",
        description=description,
        formatter_class=flux.util.help_formatter(),
        add_help=False,
    )
    alloc.parser.set_defaults(func=alloc.main)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    LOGGER.warning(
        "⚠️ WARNING flux mini will be deprecated for a future release. See the equivalent sub-commands now on the top level. ⚠️"
    )
    main()

# vi: ts=4 sw=4 expandtab
