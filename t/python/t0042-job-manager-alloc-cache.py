#!/usr/bin/env python3
###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
"""
Correctness tests for the job-manager.resource-status response cache.

Each test exercises the cache explicitly: the cache is first populated
with a known value, a lifecycle event is triggered that should invalidate
it, and then repeated RPCs verify the cache now holds the new correct
value rather than the stale one.
"""

import unittest

import flux
import flux.job
from flux.job import JobspecV1
from flux.resource import ResourceSet
from subflux import rerun_under_flux


def __flux_size():
    return 2


def allocated_nnodes(fh):
    """Return the number of nodes in the job-manager allocated set."""
    r = fh.rpc("job-manager.resource-status").get()
    return ResourceSet(r["allocated"]).nnodes


class TestResourceStatusCache(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.fh = flux.Flux()

    def submit_sleep(self, nnodes=1):
        jobspec = JobspecV1.from_command(
            ["sleep", "inf"], num_nodes=nnodes, num_tasks=nnodes
        )
        return flux.job.submit(self.fh, jobspec)

    def wait_clean(self, jobid):
        flux.job.event_wait(self.fh, jobid, "clean", raiseJobException=False)

    def test_01_cache_invalidated_on_alloc(self):
        """Cache is invalidated when a job acquires resources.

        Populate the cache with an empty allocated set, then submit a job
        and wait for the alloc event.  Repeated RPCs must reflect the new
        non-empty set, proving the cache was invalidated rather than
        serving the stale empty value.
        """
        # Populate cache with empty set.
        self.assertEqual(allocated_nnodes(self.fh), 0)

        jobid = self.submit_sleep(nnodes=1)
        try:
            flux.job.event_wait(self.fh, jobid, "alloc")
            # First RPC: cache miss after invalidation — must return 1, not stale 0.
            # Subsequent RPCs: cache hits — must remain 1.
            for _ in range(3):
                self.assertEqual(allocated_nnodes(self.fh), 1)
        finally:
            flux.job.cancel(self.fh, jobid)
            self.wait_clean(jobid)

    def test_02_cache_invalidated_on_free(self):
        """Cache is invalidated when a job's resources are freed.

        Populate the cache with a non-empty allocated set, then cancel the
        job and wait for clean.  Repeated RPCs must return empty, proving
        the cache was invalidated rather than serving the stale non-empty value.
        """
        jobid = self.submit_sleep(nnodes=1)
        flux.job.event_wait(self.fh, jobid, "alloc")

        # Populate cache with the running job's resources.
        self.assertEqual(allocated_nnodes(self.fh), 1)

        flux.job.cancel(self.fh, jobid)
        self.wait_clean(jobid)

        # First RPC: cache miss after invalidation — must return 0, not stale 1.
        # Subsequent RPCs: cache hits — must remain 0.
        for _ in range(3):
            self.assertEqual(allocated_nnodes(self.fh), 0)

    def test_03_cache_correct_across_two_alloc_free_cycles(self):
        """Cache tracks allocated set correctly across sequential job cycles.

        Run two jobs back-to-back.  After each alloc the cache must show
        the job's resources; after each free the cache must show empty.
        This exercises cache invalidation across multiple alloc/free cycles.
        """
        for _ in range(2):
            # Cache should be empty at the start of each cycle.
            self.assertEqual(allocated_nnodes(self.fh), 0)

            jobid = self.submit_sleep(nnodes=1)
            flux.job.event_wait(self.fh, jobid, "alloc")

            # Populate cache; verify it holds the correct non-empty value.
            for _ in range(2):
                self.assertEqual(allocated_nnodes(self.fh), 1)

            flux.job.cancel(self.fh, jobid)
            self.wait_clean(jobid)

    def test_04_cache_correct_with_two_concurrent_jobs(self):
        """Cache reflects the combined allocated set of concurrent jobs.

        Populate the cache with 2 nodes allocated, cancel one job and verify
        the cache is invalidated to show 1 node, then cancel the second and
        verify the cache drops to 0.
        """
        j1 = self.submit_sleep(nnodes=1)
        j2 = self.submit_sleep(nnodes=1)
        flux.job.event_wait(self.fh, j1, "alloc")
        flux.job.event_wait(self.fh, j2, "alloc")

        # Populate cache with both jobs allocated.
        for _ in range(2):
            self.assertEqual(allocated_nnodes(self.fh), 2)

        # Cancel one job; cache must be invalidated to show 1, not stale 2.
        flux.job.cancel(self.fh, j1)
        self.wait_clean(j1)
        for _ in range(2):
            self.assertEqual(allocated_nnodes(self.fh), 1)

        # Cancel the other; cache must drop to 0, not stale 1.
        flux.job.cancel(self.fh, j2)
        self.wait_clean(j2)
        for _ in range(2):
            self.assertEqual(allocated_nnodes(self.fh), 0)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(), buffer=False)
