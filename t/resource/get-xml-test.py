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


def resource_load_all_except_one(size):
    """Load all resource module except rank=size=1
    Modules must be loaded sequentially
    """
    for rank in range(0, size - 1):
        flux_module_exec(rank=str(rank), cmd="load")


def resource_load_last(size):
    rank = size - 1
    flux_module_exec(rank=str(rank), cmd="load")


### Main test program

logging.basicConfig(level=logging.INFO)
log = logging.getLogger("get-xml-test")

handle = flux.Flux()
size = int(handle.attr_get("size"))

log.info("unloading resource modules across %d ranks", size)
resource_unload_all()

log.info("reloading all resource modules except rank %d", size - 1)
resource_load_all_except_one(size)

log.info("initiating resource.get-xml RPC")
future = RPC(handle, "resource.get-xml", {})

#  Ensure get-xml initially blocks
delay = 0.5
log.info("waiting up to %.2fs for get-xml (should block)", delay)
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

log.info("loading resource module on rank %d", size - 1)
resource_load_last(size)

# Ensure get-xml can now complete
log.info("get-xml RPC should now complete")
future.wait_for(5)
log.info("done")
