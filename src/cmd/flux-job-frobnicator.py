###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os
import sys
import logging
import json
import argparse
import flux
from flux.job.frobnicator import JobFrobnicator
from flux.job import Jobspec


LOGGER = logging.getLogger("flux-job-frobnicator")


class HelpAction(argparse.Action):
    """
    Copy of argparse._HelpAction, so that `--help` can be added
    after initial parse_known_args() in JobFrobnicator constructor
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
        prog="flux-job-frobnicator",
        formatter_class=flux.util.help_formatter(),
        description="Modify Flux jobs from lines of JSON input on stdin",
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
        help="List available frobnicator plugins and exit",
    )

    frobnicator = JobFrobnicator(sys.argv[1:], parser=parser)

    if frobnicator.args.list_plugins:
        print("Available plugins:")
        for name, plugin in frobnicator.plugins.items():
            descr = ""
            if plugin.__doc__:
                descr = plugin.__doc__.partition("\n")[0]
            print(f"{name:<20}  {descr}")
        sys.exit(0)

    parser.add_argument("-h", "--help", action=HelpAction)

    try:
        frobnicator.start()
    except ValueError as exc:
        LOGGER.critical(exc)
        sys.exit(1)

    exitcode = 0

    # Ensure stdin is line buffered, with proper encoding
    for line in os.fdopen(
        sys.stdin.fileno(), "r", buffering=1, encoding="utf-8", errors="surrogateescape"
    ):
        if frobnicator.args.jobspec_only:
            line = (
                '{"jobspec":'
                + line.rstrip()
                + ',"userid":'
                + str(os.getuid())
                + ',"urgency":16,"flags":0}'
            )
        info = json.loads(line)

        # Check for valid input
        for key in ["jobspec", "userid", "urgency", "flags"]:
            if key not in info:
                LOGGER.critical("missing key %s in input", key)
                sys.exit(1)
        try:
            jobspec = Jobspec(**info["jobspec"])
        except Exception as exc:  # pylint: disable=broad-except
            result = {"errnum": 1, "errstr": f"invalid jobspec: {exc}"}
        else:
            try:
                frobnicator.frob(
                    jobspec, info["userid"], info["urgency"], info["flags"]
                )
                result = {"errnum": 0, "data": jobspec.jobspec}
            except Exception as exc:  # pylint: disable=broad-except
                result = {"errnum": 1, "errstr": str(exc)}

        print(json.dumps(result), flush=True)

    sys.exit(exitcode)


if __name__ == "__main__":
    main()
