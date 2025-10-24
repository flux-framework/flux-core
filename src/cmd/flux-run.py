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
import flux.cli.run as base

LOGGER = logging.getLogger("flux-run")


@flux.util.CLIMain(LOGGER)
def main():
    base.RunCmd(
        "flux run",
        description="run a job interactively",
    ).run_command()


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
