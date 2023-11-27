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
import os
import sys

from flux.cli import base
from flux.idset import IDset


class RunCmd(base.SubmitBaseCmd):
    """
    RunCmd is identical to SubmitCmd, except it attaches the the job
    after submission.  Some additional options are added to modify the
    attach behavior.

    Usage: flux run [OPTIONS] cmd ...
    """

    def __init__(self, prog, usage=None, description=None):
        super().__init__(prog, usage, description)
        self.parser.add_argument(
            "--wait-event",
            metavar="NAME",
            help="Pass --wait-event=NAME to flux-job attach",
        )
        self.parser.add_argument(
            "command", nargs=argparse.REMAINDER, help="Job command and arguments"
        )

    def main(self, args):
        jobid = self.submit(args)

        # Display job id on stderr if -v
        # N.B. we must flush sys.stderr due to the fact that it is buffered
        # when it points to a file, and os.execvp leaves it unflushed
        if args.verbose > 0:
            print("jobid:", jobid, file=sys.stderr)
            sys.stderr.flush()

        # Build args for flux job attach
        attach_args = ["flux-job", "attach"]
        if args.label_io:
            attach_args.append("--label-io")
        if args.verbose > 1:
            attach_args.append("--show-events")
        if args.verbose > 2:
            attach_args.append("--show-exec")
        if args.debug_emulate:
            attach_args.append("--debug-emulate")
        if args.wait_event:
            attach_args.append(f"--wait-event={args.wait_event}")
        if args.unbuffered:
            attach_args.append("--unbuffered")
        # If args.input is an idset, then pass along to attach:
        if args.input:
            try:
                in_ranks = IDset(args.input)
                attach_args.append(f"--stdin-ranks={in_ranks}")
            except (ValueError, EnvironmentError):
                #  Do nothing if ranks were not an idset, file input is
                #  handled in jobspec.
                pass
        attach_args.append(jobid.f58.encode("utf-8", errors="surrogateescape"))

        # Exec flux-job attach, searching for it in FLUX_EXEC_PATH.
        old_path = os.environ.get("PATH")
        os.environ["PATH"] = os.environ["FLUX_EXEC_PATH"]
        if old_path:
            os.environ["PATH"] += f":{old_path}"

        os.execvp("flux-job", attach_args)
