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

import flux
import flux.cli.alloc as base
import flux.job

LOGGER = logging.getLogger("flux-alloc")


@flux.util.CLIMain(LOGGER)
def main():
    description = """
    Allocate resources and start a new Flux instance. Once the instance
    has started, attach to it interactively.
    """
    base.AllocCmd(
        "flux alloc",
        usage="flux alloc [OPTIONS...] [COMMAND] [ARGS...]",
        description=description,
    ).run_command()


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
