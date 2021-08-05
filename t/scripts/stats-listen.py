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
import re
import socket
import subprocess
import os

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
    help="wait for N packets to be recieved",
    type=int,
    default=1,
)
parser.add_argument("cmd", nargs=argparse.REMAINDER)
args = parser.parse_args()

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))

if not args.no_set_host:
    os.environ["FLUX_FRIPP_STATSD"] = f"0.0.0.0:{s.getsockname()[1]}"

f = subprocess.Popen(args.cmd, env=dict(os.environ))
f.wait()

p = []

if args.search_for is not None:
    while True:
        m = s.recvfrom(1024)[0]
        if args.search_for in m.decode("utf-8"):
            p.append(m)
            break

else:
    for i in range(args.wait_for):
        p.append(s.recvfrom(1024)[0])

    if len(p) < args.wait_for:
        s.close()
        exit(-1)

if args.validate:
    metrics = str.splitlines("".join([_.decode("utf-8") for _ in p]))
    ex = re.compile("^\w+:[\+\-]*\d+\|ms|[gC]$")

    for m in metrics:
        if not ex.search(m):
            s.close()
            exit(-1)

print(p)
s.close()
