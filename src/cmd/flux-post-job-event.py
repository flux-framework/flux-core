#!/bin/false
##############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import logging

import flux
from flux.job import JobID
from flux.util import TreedictAction


def post_event(args):
    """Post an event to the job-manager.post-event.post service"""

    req = {"id": JobID(args.id), "name": args.name}
    if args.context:
        req["context"] = args.context

    try:
        flux.Flux().rpc("job-manager.post-event.post", req).get()
    except FileNotFoundError:
        raise ValueError(f"No such job {args.id}")


LOGGER = logging.getLogger("flux-job-post-event")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-job-post-event")
    parser.add_argument("id", help="jobid to which event shall be posted")
    parser.add_argument("name", help="name of event to post")
    parser.add_argument(
        "context",
        help="List of key=value pairs to set as event context",
        action=TreedictAction,
        nargs=argparse.REMAINDER,
    )
    parser.set_defaults(func=post_event)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
