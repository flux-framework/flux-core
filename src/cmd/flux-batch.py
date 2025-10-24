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
import flux.cli.batch as base
import flux.job

LOGGER = logging.getLogger("flux-batch")


@flux.util.CLIMain(LOGGER)
def main():
    # batch
    description = """
    Submit a batch SCRIPT and ARGS to be run as the initial program of
    a Flux instance.  If no batch script is provided, one will be read
    from stdin.
    """
    base.BatchCmd(
        "flux batch",
        usage="flux batch [OPTIONS...] [SCRIPT] [ARGS...]",
        description=description,
    ).run_command()


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
