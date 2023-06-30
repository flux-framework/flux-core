#!/usr/bin/env python3
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
import os
import re
import socket
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument(
    "-n",
    "--no-set-host",
    help="don't set the FLUX_FRIPP_STATSD environment variable",
    action="store_true",
)
parser.add_argument(
    "-s",
    "--search-for",
    metavar="METRIC",
    help="search for a specific metric tag",
)
parser.add_argument(
    "-V", "--validate", help="validate packet form", action="store_true"
)
parser.add_argument(
    "-w",
    "--wait-for",
    metavar="N",
    help="wait for N packets to be received",
    type=int,
    default=1,
)
parser.add_argument("cmd", nargs=argparse.REMAINDER)
args = parser.parse_args()

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))

if not args.no_set_host:
    os.environ["FLUX_FRIPP_STATSD"] = f"127.0.0.1:{s.getsockname()[1]}"

f = subprocess.Popen(args.cmd, env=dict(os.environ))
returncode = f.wait()
print(f"{args.cmd[0]} returncode = {returncode}", file=sys.stderr)

p = []

if args.search_for is not None:
    while True:
        m = s.recvfrom(1024)[0].decode("utf-8")
        print(f"checking for {args.search_for} in {m}", file=sys.stderr)
        if args.search_for in m:
            p.append(m)
            break

else:
    for i in range(args.wait_for):
        p.append(s.recvfrom(1024)[0].decode("utf-8"))

print(p)

if len(p) < args.wait_for:
    print(f"Error: Got less than {args.wait_for} packets", file=sys.stderr)
    s.close()
    exit(-1)

if args.validate:
    metrics = str.splitlines("".join(p))
    ex = re.compile(r"^\w+:[\+\-]*\d+\|ms|[gC]$")

    for m in metrics:
        if not ex.search(m):
            s.close()
            exit(-1)

s.close()
