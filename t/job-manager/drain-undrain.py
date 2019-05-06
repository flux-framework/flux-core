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

# Usage: drain-undrain.py
#
# Send a drain request immediately followed by an undrain request.
#
# Assumes that the first drain request will be canceled by the undrain request.
# Make sure there is a job in the queue to ensure the drain request does
# not immediately succeed.
#
# Exits with rc=1 if the drain request unexpectedly succeeds.

import flux
import sys

h = flux.Flux()

# Drain the queue.
# Response will not be sent until queue size reaches zero.
f = h.rpc("job-manager.drain")

# Undrain the queue.
# Response is sent after any pending drain requests are canceled.
f2 = h.rpc("job-manager.undrain")

# Receive drain response (expect fail due to cancellation)
try:
    f.get()
except EnvironmentError as e:
    print("{}: {}".format("drain", e.strerror))
else:
    print("{}: {}".format("drain", "succeeded unexpectedly"))
    sys.exit(1)

# Receive undrain response (expect success)
try:
    f2.get()
except EnvironmentError as e:
    print("{}: {}".format("undrain", e.strerror))
    sys.exit(1)
