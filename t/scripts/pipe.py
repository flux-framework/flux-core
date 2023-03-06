#!/usr/bin/env python3
###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
#
# Pipe standard input to a job, then exit.
#
import flux
from flux.job import JobID, event_wait
import sys


def pipe_stdin(h, jobid, ranks):
    event_wait(h, jobid, "start")
    event = event_wait(h, jobid, "shell.init", eventlog="guest.exec.eventlog")
    service = event.context["service"] + ".stdin"
    for line in sys.stdin:
        h.rpc(
            service,
            {"stream": "stdin", "rank": ranks, "data": line},
        ).get()
    h.rpc(service, {"stream": "stdin", "rank": ranks, "eof": True}).get()


pipe_stdin(flux.Flux(), JobID(sys.argv[1]), sys.argv[2])
