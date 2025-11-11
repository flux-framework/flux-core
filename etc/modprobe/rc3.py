#############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import os
from datetime import datetime
from pathlib import Path

from flux.modprobe import run_all_rc_scripts, task


@task("run-all-rc3", before=["*"])
def run_all_rc3(context):
    run_all_rc_scripts(3)


@task("post-finish-event", ranks="0", before=["kvs"], needs=["kvs"])
def post_finish_event(context):
    context.bash("flux startlog --post-finish-event")


@task(
    "content-dump",
    ranks="0",
    after=["content-flush"],
    before=["content-backing"],
    needs_attrs=["content.dump"],
)
def content_dump(context):
    dumpfile = context.attr_get("content.dump")
    dumplink = None
    if dumpfile == "auto":
        statedir = "."
        if context.attr_get("statedir-cleanup") == "0":
            statedir = context.attr_get("statedir")
        path = Path(statedir + "/dump")
        path.mkdir(parents=True, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        dumpfile = path / f"{timestamp}.tgz"
        dumplink = path / "RESTORE"

    print(f"dumping content to {dumpfile}")
    context.bash(
        "flux dump --sd-notify --quiet --ignore-failed-read --maxreqs=128 "
        + f"--checkpoint {dumpfile}"
    )
    if dumplink:
        os.symlink(Path(dumpfile).name, dumplink)


@task(
    "content-flush",
    ranks="0",
    after=["kvs"],
    before=["content-dump", "content-backing"],
    needs=["content-backing"],
)
def content_flush(context):
    context.rpc("content.flush").get()
