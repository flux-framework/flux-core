###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import json
import logging
import os
import sys

import flux
from flux.job.validator import JobValidator

LOGGER = logging.getLogger("flux-job-validator")


class HelpAction(argparse.Action):
    """
    Copy of argparse._HelpAction, so that `--help` can be added
    after initial parse_known_args() in JobValidator constructor
    """

    def __init__(
        self,
        option_strings,
        dest=argparse.SUPPRESS,
        default=argparse.SUPPRESS,
        xhelp=None,
    ):
        super(HelpAction, self).__init__(
            option_strings=option_strings,
            dest=dest,
            default=default,
            nargs=0,
            help=xhelp,
        )

    def __call__(self, parser, namespace, values, option_string=None):
        parser.print_help()
        parser.exit()


@flux.util.CLIMain(LOGGER)
def main():

    parser = argparse.ArgumentParser(
        prog="flux-job-validator",
        formatter_class=flux.util.help_formatter(),
        description="Validate Flux jobs from lines of JSON input on stdin",
        add_help=False,
    )
    script_group = parser.add_argument_group("Program options")
    script_group.add_argument(
        "--jobspec-only",
        action="store_true",
        help="Expect only JSON encoded jobspec on stdin",
    )
    script_group.add_argument(
        "--list-plugins",
        action="store_true",
        help="List available validator plugins and exit",
    )

    validator = JobValidator(sys.argv[1:], parser=parser)

    if validator.args.list_plugins:
        print("Available plugins:")
        for name, plugin in validator.plugins.items():
            descr = ""
            if plugin.__doc__:
                descr = plugin.__doc__.partition("\n")[0]
            print(f"{name:<20}  {descr}")
        sys.exit(0)

    parser.add_argument("-h", "--help", action=HelpAction)

    try:
        validator.start()
    except ValueError as exc:
        LOGGER.critical(exc)
        sys.exit(1)

    exitcode = 0

    # Ensure stdin is line buffered, with proper encoding
    for line in os.fdopen(
        sys.stdin.fileno(), "r", buffering=1, encoding="utf-8", errors="surrogateescape"
    ):
        if validator.args.jobspec_only:
            jobspec = json.loads(line)
            result = validator.validate(
                {
                    "jobspec": jobspec,
                    "userid": os.getuid(),
                    "flags": None,
                    "urgency": 16,
                }
            )
            #  In --jobspec-only mode, exit with nonzero exit code
            #   if validation failed:
            if result.errnum != 0:
                exitcode = 1
        else:
            result = validator.validate(line)
        print(result, flush=True)

    validator.stop()

    sys.exit(exitcode)


if __name__ == "__main__":
    main()
