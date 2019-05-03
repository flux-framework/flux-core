#!/usr/bin/env python

###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: drain-cancel.py jobid
#
# Send a blocking drain request, then cancel jobid.
# The drain response should be received, assuming the queue
# was set up with only one job in it.
#

import flux
from flux import job
from flux.job import ffi
import sys

if len(sys.argv) != 2:
    print("{}: {}".format("Usage", "drain.py jobid"))
    sys.exit(1)

h = flux.Flux()

# Drain the queue.
# Response will not be sent until queue size reaches zero.
f = h.rpc("job-manager.drain")

# Cancel the job so queue size reaches zero
payload = {"id": int(sys.argv[1]), "type": "cancel", "severity": 0}
f2 = h.rpc("job-manager.raise", payload)

# Get the drain response.
try:
    f.get()
except EnvironmentError as e:
    print("{}: {}".format("drain", e.strerror))
    sys.exit(1)
else:
    print("{}: {}".format("drain", "OK"))

# Get the cancel response.
try:
    f2.get()
except EnvironmentError as e:
    print("{}: {}".format("cancel", e.strerror))
    sys.exit(1)
