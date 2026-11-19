#!/bin/false
##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

#  flux-audit-alloc - audit allocated resources for consistency.
#
#  Mirrors resource_status_cb() in job-manager/alloc.c:
#   - active jobs with R, no "free" event, not alloc-bypass
#   - plus housekeeping allocations (job R restricted to the
#     "allocated" ranks reported in job-manager stats)
#  but checks *all* pairs for overlap, since the jobid in a
#  "duplicate allocation" error is just whichever job hit the conflict
#  first in hash iteration order, not necessarily the double-booked one.
#
#  Also compares the job-manager's allocated set with the scheduler's
#  (sched.resource-status).

import errno
import json
import logging
import os
import shlex
import subprocess
import sys
import tarfile
import time
from collections import defaultdict
from datetime import datetime
from itertools import combinations

import flux
import flux.util
from flux.cli.argparse import FluxArgumentParser
from flux.eventlog import EventLogEvent
from flux.idset import IDset
from flux.job import JobID, JobList, job_info_lookup
from flux.resource import ResourceSet

LOGGER = logging.getLogger("flux-audit-alloc")

#  Number of times, and seconds between, resampling the job-manager and
#  scheduler views when they differ, to rule out allocs and frees in flight.
RESAMPLE_COUNT = 5
RESAMPLE_INTERVAL = 1.0


class Style:
    """ANSI color and status symbol decoration, all no-ops when disabled"""

    RED = "\033[1;31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    CYAN = "\033[36m"
    RESET = "\033[0m"

    def __init__(self, enabled=False):
        self.enabled = enabled

    def color(self, text, color):
        return f"{color}{text}{self.RESET}" if self.enabled else str(text)

    def symbol(self, char):
        return f"{char} " if self.enabled else ""

    @property
    def arrow(self):
        return "\u2194" if self.enabled else "<->"

    def ok(self, msg):
        LOGGER.info(self.color(self.symbol("\u2714") + msg, self.GREEN))

    def problem(self, msg):
        LOGGER.error(self.color(self.symbol("\u2718") + msg, self.RED))

    def detail(self, label, msg):
        LOGGER.error("   %s: %s", label, msg)

    def warn(self, msg):
        LOGGER.warning(self.color(self.symbol("\u26a0") + msg, self.YELLOW))

    def resources(self, rset):
        if not rset.nnodes:
            return "none"
        return self.color(f"{rset} ({rset.nodelist})", self.CYAN)


STYLE = Style()


def ts(t):
    """ISO 8601 timestamp matching 'flux dmesg' and 'flux job eventlog -T iso'"""
    if not t:
        return "-"
    s = datetime.fromtimestamp(t).astimezone().isoformat(timespec="microseconds")
    #  libflux prints UTC as "Z", Python as "+00:00"
    return s[:-6] + "Z" if s.endswith("+00:00") else s


class Holder:
    """Something holding resources: a job allocation or housekeeping"""

    def __init__(self, kind, jobid, rset, events, info=None):
        self.kind = kind
        self.jobid = jobid
        self.rset = rset
        self.times = {e.name: e.timestamp for e in events}
        self.info = info or {}

    @property
    def name(self):
        return f"{self.kind} {self.jobid.f58}"

    def describe(self):
        """Return relevant event times (and housekeeping state)"""
        desc = [
            f"{name} {ts(self.times[name])}"
            for name in ("alloc", "free", "clean")
            if name in self.times
        ]
        if self.kind == "housekeeping":
            desc.append(
                f"housekeeping since {ts(self.info['t_start'])}"
                f" on ranks {self.info['allocated']}"
                f" (pending: {self.info['pending'] or 'none'})"
            )
        return ", ".join(desc)


def raise_if_eperm(exc, topic):
    """Abort on EPERM, since an audit of a partial view is meaningless"""
    if exc.errno == errno.EPERM:
        raise OSError(exc.errno, f"{topic}: {exc.strerror}") from None


def lookup_all(h, ids):
    """Concurrently look up R and eventlog for many jobs.
    Returns (results, errors): dicts of jobid -> (R, events) and
    jobid -> OSError.
    """
    LOGGER.debug("looking up R and eventlog for %d jobs", len(ids))
    futures = {
        jobid: job_info_lookup(h, jobid, keys=["R", "eventlog"]) for jobid in ids
    }
    results = {}
    errors = {}
    for jobid, f in futures.items():
        try:
            resp = f.get()
        except OSError as exc:
            raise_if_eperm(exc, "job-info.lookup")
            errors[jobid] = exc
            continue
        events = [EventLogEvent(x) for x in resp["eventlog"].splitlines()]
        results[jobid] = (ResourceSet(resp["R"]), events)
    return results, errors


def gather_holders(h):
    holders = []
    skipped = []

    def skip(jobid, reason):
        LOGGER.debug("skipping %s: %s", jobid.f58, reason)
        skipped.append((jobid, reason))

    #  Job-manager's view of housekeeping: {f58: {pending, allocated, t_start}}
    stats = h.rpc("job-manager.stats-get", {}).get()
    hk = stats.get("housekeeping", {}).get("running", {})
    hk_ids = {JobID(x): v for x, v in hk.items()}
    LOGGER.debug("job-manager reports %d housekeeping allocations", len(hk_ids))

    #  job-list "running" covers RUN and CLEANUP. job-list may lag the
    #  job-manager slightly; the eventlog checks below are authoritative.
    jobs = JobList(h, filters=["running"], user="all", attrs=[], max_entries=0).jobs()
    job_ids = [JobID(j.id) for j in jobs]
    LOGGER.debug("job-list reports %d running jobs", len(job_ids))

    results, errors = lookup_all(h, set(job_ids) | set(hk_ids))
    for jobid, exc in errors.items():
        skip(jobid, f"lookup failed: {exc}")

    for jobid in job_ids:
        if jobid not in results:
            continue
        rset, events = results[jobid]
        if any(e.name == "free" for e in events):
            # job-manager skips these (free_posted); resources are
            # accounted to housekeeping (or released) instead
            skip(jobid, "free posted")
            continue
        if any(
            "alloc-bypass" in e.context.get("flags", [])
            for e in events
            if e.name == "set-flags"
        ):
            skip(jobid, "alloc-bypass")
            continue
        holders.append(Holder("job", jobid, rset, events))

    for jobid, info in hk_ids.items():
        if jobid not in results:
            continue
        rset, events = results[jobid]
        rset = rset.copy_ranks(IDset(info["allocated"]))
        holders.append(Holder("housekeeping", jobid, rset, events, info))

    return holders, skipped, stats


def find_conflicts(holders):
    """Return list of (a, b, overlap) for all overlapping holder pairs"""
    #  Only intersect pairs that share at least one rank
    byrank = defaultdict(list)
    for i, holder in enumerate(holders):
        for rank in holder.rset.ranks:
            byrank[rank].append(i)
    pairs = {pair for indices in byrank.values() for pair in combinations(indices, 2)}
    LOGGER.debug("checking %d pairs of allocations that share ranks", len(pairs))
    conflicts = []
    for i, j in sorted(pairs):
        a, b = holders[i], holders[j]
        overlap = a.rset.intersect(b.rset)
        if overlap.nnodes > 0:
            conflicts.append((a, b, overlap))
    return conflicts


def report_conflicts(conflicts):
    if not conflicts:
        STYLE.ok("no overlapping allocations found")
    for a, b, overlap in conflicts:
        STYLE.problem(f"conflict: {a.name} {STYLE.arrow} {b.name}")
        STYLE.detail("overlap", STYLE.resources(overlap))
        for x in (a, b):
            STYLE.detail(x.name, x.describe())


def allocated(h, topic, when=None):
    """Return the allocated ResourceSet from a resource-status RPC to
    'topic', or None on error. 'when' labels the sample in debug output.
    """
    try:
        rset = ResourceSet(h.rpc(topic, {}).get()["allocated"])
        label = f"{topic} ({when})" if when else topic
        LOGGER.debug("%s: %d ranks allocated", label, rset.nnodes)
        return rset
    except OSError as exc:
        raise_if_eperm(exc, topic)
        STYLE.warn(f"{topic}: {exc.strerror}")
        return None


def sample_views(h, when=None):
    """Return the (job-manager, scheduler) allocated ResourceSets.
    Either may be None on error.
    """
    return (
        allocated(h, "job-manager.resource-status", when),
        allocated(h, "sched.resource-status", when),
    )


def check_sched(h, jm_rset, sched_rset, holders, resample=True):
    """Compare the job-manager's allocated set to the scheduler's.
    An alloc or free in flight between the two makes the views differ
    briefly, so if they differ, sample both again up to RESAMPLE_COUNT
    times and report only differences that persist.
    Return a mismatch dict, or None if views agree or sched unavailable.
    """
    if sched_rset is None:
        return None
    jm_only = jm_rset.diff(sched_rset)
    sched_only = sched_rset.diff(jm_rset)
    tries = RESAMPLE_COUNT if resample else 0
    for i in range(1, tries + 1):
        if not jm_only.nnodes and not sched_only.nnodes:
            break
        time.sleep(RESAMPLE_INTERVAL)
        jm, sched = sample_views(h, f"resample {i}")
        if jm is None or sched is None:
            break
        jm_only = jm_only.intersect(jm.diff(sched))
        sched_only = sched_only.intersect(sched.diff(jm))
    if not jm_only.nnodes and not sched_only.nnodes:
        STYLE.ok("job-manager and scheduler agree on allocated resources")
        return None
    STYLE.problem("scheduler mismatch: allocated in one view but not the other")
    STYLE.detail("allocated in job-manager only", STYLE.resources(jm_only))
    held_by = []
    for holder in holders:
        overlap = holder.rset.intersect(jm_only)
        if overlap.nnodes:
            STYLE.detail(f"  held by {holder.name}", STYLE.resources(overlap))
            held_by.append({"holder": holder.name, "R": str(overlap)})
    STYLE.detail("allocated in scheduler only", STYLE.resources(sched_only))
    return {
        "job_manager_only": str(jm_only),
        "job_manager_only_nodelist": str(jm_only.nodelist),
        "job_manager_only_held_by": held_by,
        "sched_only": str(sched_only),
        "sched_only_nodelist": str(sched_only.nodelist),
    }


def run_commands(outdir, commands):
    """Run each command in dict name -> cmd, saving output to outdir/name.txt"""
    os.makedirs(outdir, exist_ok=True)
    for name, cmd in commands.items():
        LOGGER.debug("running %s", cmd)
        with open(os.path.join(outdir, f"{name}.txt"), "w") as fp:
            fp.write(f"$ {cmd}\n")
            fp.flush()
            p = subprocess.run(shlex.split(cmd), stdout=fp, stderr=subprocess.STDOUT)
            if p.returncode != 0:
                fp.write(f"\n[exit {p.returncode}]\n")


def write_json(outdir, name, obj):
    LOGGER.debug("writing %s", name)
    with open(os.path.join(outdir, name), "w") as fp:
        json.dump(obj, fp, indent=2)


def collect(h, outdir, conflicts, holders, skipped, stats, mismatch):
    run_commands(
        outdir,
        {
            "version": "flux version",
            "modules": "flux module list",
            "dmesg": "flux dmesg",
            "resource-list": "flux resource list",
            "resource-status": "flux resource status",
            "resource-R": "flux resource R",
            "resource-eventlog": "flux resource eventlog -T iso",
            "housekeeping": "flux housekeeping list",
            "queue-status": "flux queue status -v",
            "jobtap": "flux jobtap list -a",
            "config": "flux config get",
            "jobs": "flux jobs -a -o long",
            "stats-sched-fluxion-resource": "flux module stats sched-fluxion-resource",
            "stats-sched-fluxion-qmanager": "flux module stats sched-fluxion-qmanager",
            "stats-sched-simple": "flux module stats sched-simple",
        },
    )
    write_json(outdir, "job-manager-stats.json", stats)

    #  Raw views of allocated resources from each side. Note that
    #  'flux resource list' answers via resource.sched-status, whose
    #  'allocated' comes from the job-manager, not the scheduler.
    for topic in ("sched.resource-status", "job-manager.resource-status"):
        try:
            write_json(outdir, f"{topic}.json", h.rpc(topic, {}).get())
        except OSError as exc:
            write_json(outdir, f"{topic}.json", {"error": exc.strerror})

    involved = {x.jobid for a, b, _ in conflicts for x in (a, b)}
    for jobid in sorted(involved):
        run_commands(
            os.path.join(outdir, "jobs", jobid.f58),
            {
                "info": f"flux jobs -o long {jobid}",
                "R": f"flux job info {jobid} R",
                "jobspec": f"flux job info {jobid} jobspec",
                "eventlog": f"flux job eventlog -T iso {jobid}",
                "exec-eventlog": f"flux job eventlog -T iso -p exec {jobid}",
                # what fluxion thinks this job holds (fails if not fluxion)
                "fluxion-info": f"flux ion-resource info {jobid.dec}",
            },
        )

    summary = {
        "conflicts": [
            {
                "a": a.name,
                "b": b.name,
                "overlap": str(o),
                "nodelist": str(o.nodelist),
            }
            for a, b, o in conflicts
        ],
        "holders": [
            {"holder": x.name, "R": str(x.rset), "info": x.info} for x in holders
        ],
        "skipped": [{"id": j.f58, "reason": r} for j, r in skipped],
        "sched_mismatch": mismatch,
    }
    write_json(outdir, "summary.json", summary)

    outdir = outdir.rstrip("/")
    tarball = f"{outdir}.tar.gz"
    with tarfile.open(tarball, "w:gz") as tar:
        tar.add(outdir, arcname=os.path.basename(outdir))
    return tarball


def parse_args():
    parser = FluxArgumentParser(
        prog="flux-audit-alloc",
        description="Check for overlapping resource allocations and for "
        "disagreement between the job-manager and scheduler. "
        "Collect a debug bundle if duplicate allocations are found.",
    )
    parser.add_argument(
        "-o",
        "--outdir",
        metavar="DIR",
        default=f"flux-audit-alloc-{time.strftime('%Y%m%d-%H%M%S')}",
        help="Write debug bundle to DIR and DIR.tar.gz",
    )
    parser.add_argument(
        "-n",
        "--no-collect",
        action="store_true",
        help="Report problems only, do not collect a debug bundle",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Log what the command is doing at debug level",
    )
    parser.add_argument("--color", action=flux.util.ColorAction)
    return parser.parse_args()


#  Reopen stderr as UTF-8 so symbols aren't escaped under a non-UTF-8
#  locale (e.g. C locale on Python 3.6). This must be done before
#  CLIMain() configures logging, which captures sys.stderr.
sys.stderr = open(sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape")


@flux.util.CLIMain(LOGGER)
def main():
    args = parse_args()
    if args.verbose:
        LOGGER.setLevel(logging.DEBUG)
    #  Messages go to stderr via LOGGER, so check stderr for 'auto'
    STYLE.enabled = args.color == "always" or (
        args.color == "auto" and sys.stderr.isatty()
    )
    h = flux.Flux()

    jm_rset, sched_rset = sample_views(h)

    holders, skipped, stats = gather_holders(h)
    conflicts = find_conflicts(holders)

    #  If job-manager.resource-status failed (as it does on a duplicate),
    #  use the union of what it would have appended instead. Resampling
    #  would fail the same way, so compare against this view only once.
    resample = jm_rset is not None
    if jm_rset is None:
        jm_rset = ResourceSet().union(*[x.rset for x in holders])

    LOGGER.info(
        "%schecked %d allocations, skipped %d",
        STYLE.symbol("\U0001f50e"),
        len(holders),
        len(skipped),
    )
    report_conflicts(conflicts)
    mismatch = check_sched(h, jm_rset, sched_rset, holders, resample)

    if conflicts and not args.no_collect:
        tarball = collect(h, args.outdir, conflicts, holders, skipped, stats, mismatch)
        LOGGER.info("%sdebug bundle: %s", STYLE.symbol("\U0001f4e6"), tarball)

    if conflicts or mismatch:
        sys.exit(1)


if __name__ == "__main__":
    main()
