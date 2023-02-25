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

import flux.cli.bulksubmit as base
import flux.utils

LOGGER = logging.getLogger("flux-bulksubmit")


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    description = """
    Submit a series of commands given on the command line or on stdin,
    using an interface similar to GNU parallel or xargs. Allows jobs to be
    submitted much faster than calling flux submit in a loop. Inputs on the
    command line are separated from each other and the command with the
    special delimiter ':::'.
    """

    # create the bulksubmit parser
    bulksubmit = base.BulkSubmitCmd(
        formatter_class=flux.util.help_formatter(),
        usage="flux bulksubmit [OPTIONS...] COMMAND [ARGS...]",
        description=description,
    )
    parser = bulksubmit.get_parser()
    parser.set_defaults(func=bulksubmit.main)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
