#!/usr/bin/env python3
###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
#
#  Follow Flux dmesg output until a line matches a pattern
#
import argparse
import re
import sys

import flux
from flux.constants import FLUX_RPC_STREAMING
from flux.core.watchers import TimerWatcher

parser = argparse.ArgumentParser(
    description="watch the flux dmesg log for a given pattern"
)
parser.add_argument(
    "-t",
    "--timeout",
    help="Timeout with error after some number of seconds",
    metavar="SEC",
    type=float,
    default=1.0,
)
parser.add_argument(
    "-v",
    "--verbose",
    help="Emit each line of dmesg output, not just first matching",
    action="count",
    default=0,
)
parser.add_argument("pattern")
args = parser.parse_args()


def timer_cb(h, watcher, revents, _arg):
    print("Timeout!", file=sys.stderr)
    h.reactor_stop_error()


def dmesg_cb(rpc, pattern, verbose=False):
    buf = rpc.get_str()
    match = pattern.search(buf)
    if match:
        print(buf)
        rpc.flux_handle.reactor_stop()
    elif verbose:
        print(buf)
    rpc.reset()


pattern = re.compile(args.pattern)

h = flux.Flux()

rpc = h.rpc("log.dmesg", {"follow": True}, flags=FLUX_RPC_STREAMING)
rpc.then(dmesg_cb, pattern, verbose=args.verbose)

TimerWatcher(h, args.timeout, timer_cb).start()
if h.reactor_run() < 0:
    sys.exit(1)
