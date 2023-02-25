##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import logging
import sys

import flux
import flux.cli.alloc as base
import flux.job

LOGGER = logging.getLogger("flux-alloc")


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    description = """
    Allocate resources and start a new Flux instance. Once the instance
    has started, attach to it interactively.
    """

    alloc = base.AllocCmd(
        help="allocate a new instance for interactive use",
        usage="flux alloc [COMMAND] [ARGS...]",
        description=description,
        formatter_class=flux.util.help_formatter(),
    )
    parser = alloc.get_parser()
    parser.set_defaults(func=alloc.main)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
