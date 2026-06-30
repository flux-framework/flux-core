#!/usr/bin/env python3
##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################
#
# Online GC soak / stress test.
#
# Drives several activities concurrently against a running instance and
# repeatedly runs "flux gc" followed by "flux fsck", proving that no
# referenced data is ever swept under sustained, realistic load.  This
# stresses the *interactions* between the live-instance safety claims in
# doc/guide/kvs_gc.rst that the targeted sharness tests only hit one at a
# time:
#
#   - commits during GC (new stores stamped epoch >= H, never swept)
#   - dedup re-reference race (re-referencing an old garbage blob re-stores it)
#   - graft hazard (completing jobs graft their guest namespace by reference)
#   - checkpoint churn (periodic checkpoints advance H and prune old ones)
#   - concurrent GC runs
#
# Like throughput.py, this connects to an already-running instance; run it
# under "flux run", "flux batch", or an interactive "flux start".  It is meant
# to be run by hand or in a nightly soak job, NOT as part of "make check" --
# it is time-bounded and load-heavy.
#
# The hazards are exercised most aggressively when the instance is configured
# for rapid checkpoint churn (a short kvs.checkpoint-period and a small
# content-sqlite.max_checkpoints); with the defaults the test still runs but
# advances the epoch and prunes checkpoints more slowly.  For example:
#
#   cat >gc.toml <<EOF
#   [kvs]
#   checkpoint-period = "200ms"
#   [content-sqlite]
#   max_checkpoints = 2
#   EOF
#   flux start --config-path=$(pwd) \
#       flux python src/test/gc-soak.py --duration=60
#
# Oracle: after every gc+fsck cycle, gc must exit 0, fsck must exit 0, and a
# set of never-deleted "canary" keys must read back their exact values --
# verified at the end through a COLD cache (content dropcache) so that a
# swept-but-still-cached blob cannot mask data loss.

import argparse
import os
import subprocess
import sys
import threading
import time

import flux
from flux import job
from flux.job import JobspecV1


def parse_args():
    parser = argparse.ArgumentParser(
        description="Online GC soak/stress test (run inside a flux instance)"
    )
    parser.add_argument(
        "--duration",
        type=float,
        metavar="SECONDS",
        default=60.0,
        help="Total soak duration in seconds (default 60)",
    )
    parser.add_argument(
        "--gc-interval",
        type=float,
        metavar="SECONDS",
        default=2.0,
        help="Seconds between gc+fsck cycles (default 2)",
    )
    parser.add_argument(
        "--canaries",
        type=int,
        metavar="N",
        default=50,
        help="Number of never-deleted keys to verify at the end (default 50)",
    )
    parser.add_argument(
        "--exec",
        action="store_true",
        help="Actually run jobs instead of simulating execution",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Log each gc+fsck cycle and activity progress",
    )
    return parser.parse_args()


# Bigger than BLOBREF_MAX_STRING_SIZE (72) so values are stored as separate
# valref blobs rather than inlined -- the case the graft hazard depends on.
BIGVAL = "soak-value-" + ("x" * 80)


class Activity(threading.Thread):
    """Base class for a background activity loop bounded by a stop event.

    Each activity opens its own flux handle inside run(): a flux handle wraps
    a single connection and is not safe to share across threads.
    """

    def __init__(self, stop, verbose=False):
        super().__init__(daemon=True)
        self.stop = stop
        self.verbose = verbose
        self.handle = None
        self.error = None
        self.iterations = 0

    def step(self):
        raise NotImplementedError

    def run(self):
        try:
            self.handle = flux.Flux()
            while not self.stop.is_set():
                self.step()
                self.iterations += 1
        except Exception as exc:  # noqa: BLE001
            self.error = exc


class KVSChurn(Activity):
    """Put and unlink keys, creating genuine garbage and dedup re-references.

    Re-puts the same large value under rotating keys (dedup re-reference of an
    old blob) and unlinks earlier keys (creating garbage below the horizon).
    """

    def step(self):
        i = self.iterations
        slot = i % 16
        flux.kvs.put(self.handle, f"soak.churn.{slot}.big", BIGVAL)
        flux.kvs.put(self.handle, f"soak.churn.{slot}.small", i)
        # Re-reference the same big value under a rotating key (dedup
        # re-reference of an existing blob), and unlink the previous
        # occupant of this slot to create unreferenced garbage below H.
        flux.kvs.put(self.handle, f"soak.dedup.{slot}", BIGVAL)
        if i >= 16:
            flux.kvs.put_unlink(self.handle, f"soak.churn.{slot}.big")
            flux.kvs.put_unlink(self.handle, f"soak.churn.{slot}.small")
        flux.kvs.commit(self.handle)
        time.sleep(0.005)


class NamespaceChurn(Activity):
    """Create private namespaces, write to them, graft+remove some.

    Mirrors what job-exec does on job completion: copy the namespace into the
    primary tree by reference (flux kvs copy) then remove the namespace -- the
    graft hazard, driven without needing the job machinery.
    """

    def step(self):
        i = self.iterations
        ns = f"soak-ns-{i}"
        subprocess.run(
            ["flux", "kvs", "namespace", "create", ns],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            ["flux", "kvs", "put", f"--namespace={ns}", f"old={BIGVAL}"],
            check=True,
            capture_output=True,
        )
        # Graft into the primary tree by reference, then destroy the namespace.
        subprocess.run(
            [
                "flux",
                "kvs",
                "copy",
                f"--src-namespace={ns}",
                ".",
                f"soak.graft.{i % 8}",
            ],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            ["flux", "kvs", "namespace", "remove", ns],
            check=True,
            capture_output=True,
        )
        time.sleep(0.02)


class JobChurn(Activity):
    """Submit short jobs; each completing job grafts+destroys a guest ns."""

    def __init__(self, stop, exec_jobs=False, verbose=False):
        super().__init__(stop, verbose)
        self.exec_jobs = exec_jobs

    def step(self):
        jobspec = JobspecV1.from_command(["true"])
        jobspec.cwd = os.getcwd()
        jobspec.environment = dict(os.environ)
        if not self.exec_jobs:
            jobspec.setattr("system.exec.test.run_duration", "0.001s")
        jobid = job.submit(self.handle, jobspec, waitable=True)
        job.wait(self.handle, jobid)
        time.sleep(0.01)


def run_cmd(args):
    """Run a flux subcommand, returning (rc, combined output)."""
    proc = subprocess.run(args, capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def gc_fsck_cycle(concurrent):
    """Run gc (optionally two at once) then fsck.  Returns (ok, message)."""
    if concurrent:
        p1 = subprocess.Popen(
            ["flux", "gc"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        p2 = subprocess.Popen(
            ["flux", "gc"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        out1 = p1.communicate()[0].decode()
        out2 = p2.communicate()[0].decode()
        if p1.returncode != 0 or p2.returncode != 0:
            return False, f"concurrent gc failed:\n{out1}\n{out2}"
    else:
        rc, out = run_cmd(["flux", "gc"])
        if rc != 0:
            return False, f"gc failed (rc={rc}):\n{out}"
    rc, out = run_cmd(["flux", "fsck"])
    if rc != 0:
        return False, f"fsck failed (rc={rc}):\n{out}"
    return True, None


def write_canaries(handle, n):
    """Write n never-deleted keys with known values; return {key: value}."""
    canaries = {}
    for i in range(n):
        key = f"soak.canary.{i}"
        # Mix of large (valref) and small (inline) values.
        val = f"canary-{i}-" + ("y" * (80 if i % 2 == 0 else 4))
        flux.kvs.put(handle, key, val)
        canaries[key] = val
    flux.kvs.commit(handle)
    return canaries


def verify_canaries(handle, canaries):
    """Verify every canary reads back its exact value.  Returns list of bad."""
    bad = []
    for key, expect in canaries.items():
        try:
            got = flux.kvs.get(handle, key)
        except (KeyError, OSError) as exc:
            bad.append((key, f"<unreadable: {exc}>", expect))
            continue
        if got != expect:
            bad.append((key, got, expect))
    return bad


def main():
    args = parse_args()

    handle = flux.Flux()

    print(f"gc-soak: writing {args.canaries} canary keys")
    canaries = write_canaries(handle, args.canaries)
    # Force the canaries (and a checkpoint) out to the backing store so they
    # are genuinely subject to sweep, not merely cache-resident.
    run_cmd(["flux", "kvs", "sync"])
    run_cmd(["flux", "content", "flush"])

    stop = threading.Event()
    activities = [
        KVSChurn(stop, args.verbose),
        NamespaceChurn(stop, args.verbose),
        JobChurn(stop, args.exec, args.verbose),
    ]

    for a in activities:
        a.start()
        print(f"gc-soak: started activity {type(a).__name__}")

    print(
        f"gc-soak: running for {args.duration}s "
        f"(gc+fsck every {args.gc_interval}s, "
        f"{len(activities)} activities)"
    )

    start = time.time()
    cycle = 0
    failure = None
    try:
        while time.time() - start < args.duration:
            time.sleep(args.gc_interval)
            cycle += 1
            # Every third cycle, run two gc processes at once to exercise the
            # concurrent-GC safety claim.
            concurrent = cycle % 3 == 0
            ok, msg = gc_fsck_cycle(concurrent)
            if args.verbose:
                iters = ", ".join(
                    f"{type(a).__name__}={a.iterations}" for a in activities
                )
                print(f"gc-soak: cycle {cycle} ok ({iters})")
            if not ok:
                failure = f"cycle {cycle}: {msg}"
                break
            # Surface any activity-thread exception promptly.
            for a in activities:
                if a.error is not None:
                    failure = f"activity {type(a).__name__} failed: {a.error}"
                    break
            if failure:
                break
    finally:
        stop.set()
        for a in activities:
            a.join(timeout=10)
            print(
                f"gc-soak: stopped activity {type(a).__name__} "
                f"after {a.iterations} iterations"
            )

    if failure:
        print(f"gc-soak: FAIL: {failure}", file=sys.stderr)
        return 1

    # Final integrity check through a COLD cache: drop the content cache so
    # canary reads must fault from the backing store.  A swept canary blob
    # cannot be masked by a cache hit here.
    print("gc-soak: final cold-cache canary verification")
    run_cmd(["flux", "content", "flush"])
    run_cmd(["flux", "content", "dropcache"])
    bad = verify_canaries(handle, canaries)
    if bad:
        print(
            f"gc-soak: FAIL: {len(bad)} canary key(s) lost or corrupted:",
            file=sys.stderr,
        )
        for key, got, expect in bad[:10]:
            print(f"  {key}: got {got!r} expected {expect!r}", file=sys.stderr)
        return 1

    rc, out = run_cmd(["flux", "fsck"])
    if rc != 0:
        print(f"gc-soak: FAIL: final fsck failed:\n{out}", file=sys.stderr)
        return 1

    total_iters = sum(a.iterations for a in activities)
    print(
        f"gc-soak: PASS: {cycle} gc+fsck cycles, "
        f"{total_iters} activity iterations, "
        f"{len(canaries)} canaries intact"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
