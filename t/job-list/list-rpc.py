###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: flux python list-rpc.py < payload.json
#
#  Send constructed payloads to job-list interfaces and
#  print errors for benefit of testing

import flux
import sys

h = flux.Flux()
payload = sys.stdin.read()
if len(sys.argv) > 1:
    name = sys.argv[1]
else:
    name = "list"
try:
    print(h.rpc(f"job-list.{name}", payload).get())
except OSError as err:
    print(f"errno {err.errno}: {err.strerror}")


# vim: tabstop=4 shiftwidth=4 expandtab
