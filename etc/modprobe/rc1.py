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
from pathlib import Path
from subprocess import Popen

from flux.modprobe import run_all_rc_scripts, task


def setup(context):

    backing_module = context.attr_get("content.backing-module")
    if backing_module is not None:
        context.set_alternative("content-backing", backing_module)

    context.load_modules(
        [
            "content",
            "heartbeat",
            "cron",
            "content-backing",
            "kvs",
            "kvs-watch",
            "sdbus",
            "sdbus-sys",
            "sdexec",
            "sdmon",
            "resource",
            "job-manager",
            "job-list",
            "job-info",
            "job-ingest",
            "job-exec",
        ]
    )

    # Legacy config: If FLUX_SCHED_MODULE=none, no sched modules are loaded.
    if os.environ.get("FLUX_SCHED_MODULE") != "none":
        context.load_modules(["sched", "feasibility"])


@task(
    "config-reload",
    ranks=">0",
    needs_attrs=["config.path"],
    before=["*"],
)
def config_reload(context):
    context.rpc("config.reload").get()


@task(
    "check-restore",
    ranks="0",
    before=["content-backing"],
    needs=["content-backing"],
    needs_attrs=["content.restore"],
)
def check_restore(context):
    dumpfile = context.attr_get("content.restore")
    dumplink = None

    if dumpfile == "auto":
        statedir = "."
        if context.attr_get("statedir-cleanup") == "0":
            statedir = context.attr_get("statedir")
        dumplink = Path(f"{statedir}/dump/RESTORE")
        if dumplink.is_symlink():
            dumpfile = dumplink.resolve()
        else:
            dumpfile = None
            dumplink = None

    if dumpfile:
        dumpfile = Path(dumpfile)
        context.setopt("content-backing", "truncate")

    context.set("dumpfile", dumpfile)
    context.set("dumplink", dumplink)


@task(
    "restore",
    ranks="0",
    before=["kvs"],
    after=["content-backing", "check-restore"],
    requires=["check-restore", "content-backing", "kvs"],
    needs=["content-backing", "check-restore"],
    needs_attrs=["content.restore"],
)
def restore(context):
    dumpfile = context.get("dumpfile", None)
    dumplink = context.get("dumplink", None)
    if not dumpfile:
        return
    print(f"restoring content from {dumpfile}")
    if dumpfile.exists():
        cmd = f"flux restore --quiet --checkpoint --size-limit=100M {dumpfile}"
        context.bash(cmd)
    if dumplink and dumplink.exists():
        dumplink.unlink()


@task(
    "post-start-event",
    ranks="0",
    requires=["kvs"],
    after=["content-backing", "kvs"],
    needs=["content-backing"],
)
def post_start_event(context):
    context.bash("flux startlog --post-start-event")


@task(
    "check-clean-shutdown",
    ranks="0",
    after=["post-start-event"],
    requires=["post-start-event"],
    needs=["content-backing"],
)
def check_clean_shutdown(context):
    context.bash(
        """
    if ! flux startlog --check --quiet; then
        echo "Flux was not shut down properly.  Data may have been lost."
    fi
    """
    )


@task(
    "cron-load",
    ranks="0",
    after=["cron"],
    needs=["cron"],
    needs_attrs=["cron.directory"],
)
def cron_load(context):
    path = Path(context.attr_get("cron.directory"))
    if path.is_dir():
        for file in path.iterdir():
            if file.is_file():
                returncode = Popen(f"flux cron tab < {file}", shell=True).wait()
                if returncode != 0:
                    raise OSError(f"could not load crontab: {file}")


@task("push-cleanup", ranks="0")
def push_cleanup(context):
    if "FLUX_DISABLE_JOB_CLEANUP" in os.environ:
        return
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


@task("run-all-rc1", after=["*"])
def run_all_rc1(context):
    run_all_rc_scripts(1)
