##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import json
import os
import subprocess

import flux
import flux.kvs
from flux.idset import IDset
from flux.modprobe import task


def setup(context):
    modules = [
        "content",
        "content-backing",
        "kvs",
        "kvs-watch",
        "job-manager",
        "job-ingest",
        "job-info",
        "job-list",
        "barrier",
        "heartbeat",
        "resource",
        "sched-simple",
    ]

    if os.environ.get("TEST_UNDER_FLUX_NO_JOB_EXEC") != "y":
        modules.append("job-exec")

    context.setopt("resource", "noverify")
    context.load_modules(modules)


@task("sched-simple-mode", ranks="0", before=["sched-simple"])
def sched_simple_mode(context):
    mode = os.environ.get("TEST_UNDER_FLUX_SCHED_SIMPLE_MODE", "limited=8")
    context.setopt("sched-simple", f"mode={mode}")


@task(
    "sched-simple-setdebug",
    ranks="0",
    after=["sched-simple"],
)
def sched_simple_setdebug(context):
    # --setbit 0x2 enables creation of reason_pending field
    context.rpc("sched-simple.debug", {"op": "setbit", "flags": 0x2}).get()


@task(
    "set-fake-resources",
    ranks="0",
    before=["resource"],
    after=["kvs"],
)
def set_fake_resources(context):

    cores = int(os.environ.get("TEST_UNDER_FLUX_CORES_PER_RANK", 2))
    size = int(context.attr_get("size"))
    core_idset = IDset().set(0, cores - 1)
    rank_idset = IDset().set(0, size - 1)
    R = (
        subprocess.run(
            ["flux", "R", "encode", f"-r{rank_idset}", f"-c{core_idset}"],
            stdout=subprocess.PIPE,
        )
        .stdout.decode("utf-8")
        .rstrip()
    )
    flux.kvs.put(context.handle, "resource.R", json.loads(R))
    print(f"setting fake resources {R}")
    flux.kvs.commit(context.handle)


@task(
    "push-cleanup",
    ranks="0",
)
def push_cleanup(context):
    context.rpc(
        "runat.push",
        {
            "name": "cleanup",
            "commands": [
                "flux queue idle --quiet",
                "flux cancel --user=all --quiet --states RUN",
                "flux resource acquire-mute",
                "flux queue stop --quiet --all --nocheckpoint",
            ],
        },
    ).get()
