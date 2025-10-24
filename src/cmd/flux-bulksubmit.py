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

import flux.cli.bulksubmit as base
import flux.utils

LOGGER = logging.getLogger("flux-bulksubmit")


@flux.util.CLIMain(LOGGER)
def main():
    description = """
    Submit a series of commands given on the command line or on stdin,
    using an interface similar to GNU parallel or xargs. Allows jobs to be
    submitted much faster than calling flux submit in a loop. Inputs on the
    command line are separated from each other and the command with the
    special delimiter ':::'.
    """
    base.BulkSubmitCmd(
        "flux bulksubmit",
        description=description,
    ).run_command()


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
