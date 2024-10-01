##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import asyncio
import itertools
import logging
import os
import signal
import subprocess
import sys
from pathlib import Path

import flux
from flux.future import Future
from flux.idset import IDset
from flux.job import JobID
from flux.resource import ResourceSet


def offline_ranks(handle):
    return IDset(handle.rpc("resource.status", nodeid=0).get()["offline"])


def fetch_job_ranks(handle, jobid):
    """Fetch job ranks from KVS for jobid"""
    try:
        return IDset(ResourceSet(flux.kvs.get(handle, f"{jobid.kvs}.R")).ranks)
    except FileNotFoundError:
        LOGGER.error("R not found in kvs for job %s, unable to continue", jobid)
        return None


def drain(handle, ids, reason):
    """Drain ranks in idset `ids` with reason"""

    handle.rpc(
        "resource.drain",
        {"targets": str(ids), "reason": reason, "mode": "update"},
        nodeid=0,
    )


def unblock_signals():
    """Unblock signals in children"""
    signal.pthread_sigmask(signal.SIG_UNBLOCK, {signal.SIGTERM, signal.SIGINT})


async def get_children(pid):
    """
    Return a best effort list of pid and all current descendants
    """
    result = [pid]
    p = await asyncio.create_subprocess_exec(
        "ps", "-o", "pid=", f"--ppid={pid}", stdout=subprocess.PIPE
    )
    output, errors = await p.communicate()
    if output is None:
        # No children found at this time, just return [pid]:
        return result
    # Parse ps(1) output into list of child pids:
    pids = [int(x) for x in output.decode().splitlines()]

    # Call this function recursively for all children and wait for results:
    futures = [get_children(pid) for pid in pids]
    children = await asyncio.gather(*futures)

    # Extend results with all current descendants:
    result.extend(itertools.chain(pids, *children))
    return result


async def kill_process_tree(parent_pid, sig):
    """
    Send sig to entire process tree rooted at parent_pid
    """
    # Iterate the list in reverse so that parent_pid comes last and
    # (for the most part) children are signaled before their parent:
    for pid in reversed(await get_children(parent_pid)):
        try:
            os.kill(pid, sig)
        except ProcessLookupError:
            # No error if process no longer exists
            pass


async def run_with_timeout(cmd, label, timeout=1800.0):
    """
    Run a command with a timeout using asyncio. Default timeout is 30m
    """
    p = await asyncio.create_subprocess_exec(
        *cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=unblock_signals
    )
    p.label = label
    p.canceled = False
    try:
        stdout, stderr = await asyncio.wait_for(p.communicate(), timeout)
    except asyncio.TimeoutError:
        #  On timeout, mark and cancel process and wait for 5 seconds before
        #  sending SIGKILL:
        await kill_process_tree(p.pid, signal.SIGTERM)
        p.canceled = True
        try:
            stdout, stderr = await asyncio.wait_for(p.communicate(), 5.0)
        except asyncio.TimeoutError:
            await kill_process_tree(p.pid, signal.SIGKILL)
            stdout, stderr = await p.communicate()
    p.errors = stderr.decode("utf8", errors="surrogateescape").splitlines()
    await p.wait()
    return p


def job_raise(handle, jobid, message):
    try:
        Future(handle.flux_job_raise(jobid, "prolog", 1, message)).get()
    except OSError as exc:
        LOGGER.error(f"Failed to raise exception: {message}: {exc}")


def plural(sequence):
    if len(sequence) > 1:
        return "s"
    return ""


async def run_per_rank(name, jobid, args):
    """Run args.exec_per_rank on every rank of jobid

    If command fails on any rank then drain that rank
    """
    returncode = 0

    if args.exec_per_rank is None:
        return 0

    per_rank_cmd = args.exec_per_rank.split(",")

    fail_ids = IDset()
    timeout_ids = IDset()

    handle = flux.Flux()
    hostlist = flux.hostlist.Hostlist(handle.attr_get("hostlist"))

    ranks = fetch_job_ranks(handle, jobid)
    if ranks is None:
        return 1

    #  Check for any offline ranks and subtract them from targets.
    #  Optionally drain offline ranks with a unique message that prolog/epilog
    #  failed due to offline state:
    #
    offline = offline_ranks(handle) & ranks
    if offline:
        returncode = 1
        LOGGER.warning(
            "%s: %s: rank%s %s offline. Skipping.",
            jobid,
            name,
            plural(offline),
            offline,
        )
        #  If this is a prolog, the job will get a fatal exception after the
        #  prolog finishes, but the exception will be generic such as:
        #
        #  "prolog exited with exit code=1".
        #
        #  We can't raise a fatal exception here, because that would terminate
        #  this script and other prologs would not finish. So, raise a
        #  nonfatal exception to give a hint to the user and admins why the
        #  prolog failed:
        #
        if name == "prolog":
            job_raise(handle, jobid, f"rank{plural(offline)} {offline} offline")
        ranks.subtract(offline)
        if args.drain_offline:
            drain(handle, offline, f"offline for {jobid} {name}")

    if args.verbose:
        LOGGER.info(
            "%s: %s: executing %s on ranks %s", jobid, name, per_rank_cmd, ranks
        )

    cmds = {}
    for rank in ranks:
        cmdv = ["flux", "exec", "-qn", f"-r{rank}"]
        if args.with_imp:
            cmdv.append("--with-imp")
        if args.sdexec:
            cmdv.append("--service=sdexec")
            cmdv.append(f"--setopt=SDEXEC_NAME={name}-{rank}-{jobid.f58plain}.service")
            cmdv.append(f"--setopt=SDEXEC_PROP_Description=System {name} script")
        cmds[rank] = cmdv + per_rank_cmd

    processes = [
        run_with_timeout(cmd, rank, args.timeout) for rank, cmd in cmds.items()
    ]
    results = await asyncio.gather(*processes)

    for proc in results:
        rank = proc.label
        rc = proc.returncode
        for line in proc.errors:
            LOGGER.error("%s (rank %d): %s", hostlist[rank], rank, line)
        if proc.canceled:
            timeout_ids.set(rank)
            rc = 128 + signal.SIGTERM
        elif rc > 0 and rc <= 128:
            #  process failed with non-zero exit code. Add this rank to
            #  the failed set which will be drained.
            fail_ids.set(rank)
        else:
            #  process was signaled (returncode < 0) or shell reported it
            #  was signaled (128+n). Do nothing in this case.
            pass
        if rc > returncode:
            returncode = rc

    if len(fail_ids) > 0:
        LOGGER.error("%s: rank %s failed %s, draining", jobid, fail_ids, name)
        drain(handle, fail_ids, f"{name} failed for jobid {jobid}")
    if len(timeout_ids) > 0:
        LOGGER.error("%s: rank %s %s timeout, draining", jobid, timeout_ids, name)
        drain(handle, timeout_ids, f"{name} timed out for jobid {jobid}")

    if args.verbose:
        ranks.subtract(fail_ids)
        ranks.subtract(timeout_ids)
        if len(ranks) > 0:
            LOGGER.info("%s: %s: completed successfully on %s", jobid, name, ranks)

    return returncode


async def run_scripts(name, jobid, args):
    """Run a directory of scripts in parallel"""

    returncode = 0
    jobid = JobID(os.environ["FLUX_JOB_ID"])

    if args.exec_directory is None:
        args.exec_directory = f"/etc/flux/system/{name}.d"
    path = Path(args.exec_directory)
    if not path.is_dir():
        return 0

    scripts = [x for x in path.iterdir() if os.access(x, os.X_OK)]

    if args.verbose:
        LOGGER.info(
            "%s: %s: running %d scripts from %s",
            jobid,
            name,
            len(scripts),
            args.exec_directory,
        )
    processes = [
        run_with_timeout([str(cmd)], cmd.name, args.timeout) for cmd in scripts
    ]
    results = await asyncio.gather(*processes)

    for proc in results:
        rc = proc.returncode
        cmd = proc.label
        if proc.canceled:
            LOGGER.error("%s: %s timeout", jobid, cmd)
            rc = 128 + signal.SIGTERM
        elif rc != 0:
            LOGGER.error("%s: %s failed with rc=%d", jobid, cmd, rc)
        elif args.verbose:
            LOGGER.info("%s: %s completed successfully", jobid, cmd)
        if rc > returncode:
            returncode = rc

    return returncode


async def run(name, jobid, args):
    tasks = [run_scripts(name, jobid, args)]
    if args.exec_per_rank:
        tasks.append(run_per_rank(name, jobid, args))
    sys.exit(max(await asyncio.gather(*tasks)))


def run_perilog(name, jobid, args):
    asyncio.get_event_loop().run_until_complete(run(name, jobid, args))


def run_prolog(jobid, args):
    run_perilog("prolog", jobid, args)


def run_epilog(jobid, args):
    run_perilog("epilog", jobid, args)


LOGGER = logging.getLogger("flux-perilog-run")


def main():

    signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGTERM, signal.SIGINT})

    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    prolog_parser = subparsers.add_parser(
        "prolog", formatter_class=flux.util.help_formatter()
    )
    prolog_parser.add_argument(
        "-v", "--verbose", help="Log script events", action="store_true"
    )
    prolog_parser.add_argument(
        "-t", "--timeout", help="Set a command timeout", metavar="FSD"
    )
    prolog_parser.add_argument(
        "-s", "--sdexec", help="Use sdexec to run prolog", action="store_true"
    )
    prolog_parser.add_argument(
        "-e",
        "--exec-per-rank",
        metavar="CMD[,ARGS...]",
        help="Use flux-exec(1) to run CMD and optional ARGS on each target "
        + "rank of jobid set in FLUX_JOB_ID environment variable",
    )
    prolog_parser.add_argument(
        "--with-imp",
        action="store_true",
        help="With --exec-per-rank, run CMD under 'flux-imp run'",
    )
    prolog_parser.add_argument(
        "-D",
        "--drain-offline",
        help="With --exec-per-rank, drain any offline ranks with a specific "
        + "drain message. The default is to skip offline ranks and not update "
        + "drain reason.",
        action="store_true",
    )
    prolog_parser.add_argument(
        "-d",
        "--exec-directory",
        metavar="DIRECTORY",
        help="Run all executables in DIRECTORY (default=/etc/flux/system/prolog.d)",
    )
    prolog_parser.set_defaults(func=run_prolog)

    epilog_parser = subparsers.add_parser(
        "epilog", formatter_class=flux.util.help_formatter()
    )
    epilog_parser.add_argument(
        "-v", "--verbose", help="Log script events", action="store_true"
    )
    epilog_parser.add_argument(
        "-t", "--timeout", help="Set a command timeout", metavar="FSD"
    )
    epilog_parser.add_argument(
        "-s", "--sdexec", help="Use sdexec to run epilog", action="store_true"
    )
    epilog_parser.add_argument(
        "-e",
        "--exec-per-rank",
        metavar="CMD[,ARGS...]",
        help="Use flux-exec(1) to run CMD and optional ARGS on each target "
        + "rank of jobid set in FLUX_JOB_ID environment variable",
    )
    epilog_parser.add_argument(
        "--with-imp",
        action="store_true",
        help="With --exec-per-rank, run CMD under 'flux-imp run'",
    )
    epilog_parser.add_argument(
        "-D",
        "--drain-offline",
        help="With --exec-per-rank, drain any offline ranks with a specific "
        + "drain message. The default is to skip offline ranks and not update "
        + "drain reason.",
        action="store_true",
    )
    epilog_parser.add_argument(
        "-d",
        "--exec-directory",
        metavar="DIRECTORY",
        help="Run all executables in DIRECTORY (default=/etc/flux/system/epilog.d)",
    )
    epilog_parser.set_defaults(func=run_epilog)

    args = parser.parse_args()
    try:
        jobid = JobID(os.environ["FLUX_JOB_ID"])
    except KeyError:
        LOGGER.error("FLUX_JOB_ID not found in environment")
        sys.exit(1)

    if args.timeout is not None:
        args.timeout = flux.util.parse_fsd(args.timeout)

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
        # But, avoid DEBUG messages from asyncio
        logging.getLogger("asyncio").setLevel(logging.WARNING)

    args.func(jobid, args)


if __name__ == "__main__":
    main()
