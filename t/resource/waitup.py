###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import sys
import errno
import logging
import subprocess

import flux
from flux.rpc import RPC


def flux_module_exec(rank="all", cmd="unload", opt=None):
    command = ["flux", "exec", "-r", str(rank), "flux", "module", cmd]
    if opt:
        command.append(opt)
    command.append("resource")
    subprocess.check_call(command)


def resource_unload_all():
    flux_module_exec(cmd="unload", opt="-f")


def resource_load(ranks):
    flux_module_exec(cmd="load", rank=ranks)


### Main test program


logging.basicConfig(level=logging.INFO)
log = logging.getLogger("waitup-test")

handle = flux.Flux()
size = int(handle.attr_get("size"))

log.info("unloading resource modules across %d ranks", size)
resource_unload_all()

log.info("reloading resource module on rank 0")
resource_load(0)

log.info("initiating RPC to wait for %d ranks", size)
future = RPC(handle, "resource.monitor-waitup", {"up": size})

#  Ensure waitup initially blocks
delay = 0.5
log.info("waiting up to %.2fs for RPC (should block)", delay)
try:
    future.wait_for(delay)
except OSError as err:
    if err.errno == errno.ETIMEDOUT:
        pass
    else:
        raise err

if future.is_ready():
    log.error("resource.get-xml returned before expected")
    sys.exit(1)

log.info("loading resource module on %d remaining ranks", size - 1)
for rank in range(1, size):
    resource_load(rank)

# Ensure waitup can now complete
log.info("waiting 5s for waitup RPC to complete")
future.wait_for(5)
log.info("done")
