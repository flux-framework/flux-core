#!/usr/bin/env python3

"""Tests for the flux.asyncio package."""

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os
import unittest
import asyncio

import flux.job
import flux.constants
import flux.asyncio.job


def __flux_size():
    return 1


def test_coroutine(coro):
    asyncio.get_event_loop().run_until_complete(_wrap_coroutine(coro))


async def _wrap_coroutine(coro):
    flux.asyncio.start_flux_loop(flux.Flux())
    try:
        result = await coro
    finally:
        flux.asyncio.stop_flux_loop()
    return result


class TestFluxAsyncio(unittest.TestCase):
    """Tests for the flux.asyncio package."""

    def test_submit_success(self):
        test_coroutine(self._test_submit_success())

    async def _test_submit_success(self):
        jobspec = flux.job.JobspecV1.from_command(["true"])
        jobid = await flux.asyncio.job.submit(jobspec, waitable=True)
        self.assertGreater(jobid, 0)
        exit_status = await flux.asyncio.job.wait(jobid)
        self.assertTrue(exit_status.success)
        self.assertEqual(exit_status.jobid, jobid)

    def test_submit_failure(self):
        test_coroutine(self._test_submit_failure())

    async def _test_submit_failure(self):
        jobspec = flux.job.JobspecV1.from_command(["false"])
        jobid = await flux.asyncio.job.submit(jobspec)
        self.assertGreater(jobid, 0)
        job_info = await flux.asyncio.job.result(jobid)
        self.assertEqual(job_info.id, jobid)
        self.assertEqual(job_info.returncode, 1)
        self.assertGreater(job_info.t_run, 0)

    def test_submit_cancel(self):
        test_coroutine(self._test_submit_cancel())

    async def _test_submit_cancel(self):
        jobspec = flux.job.JobspecV1.from_command(["sleep", "2"])
        jobid = await flux.asyncio.job.submit(jobspec, waitable=True)
        self.assertGreater(jobid, 0)
        await flux.asyncio.job.cancel(jobid)
        exit_status = await flux.asyncio.job.wait(jobid)
        self.assertFalse(exit_status.success)
        self.assertEqual(exit_status.jobid, jobid)

    def test_submit_events(self):
        test_coroutine(self._test_submit_events())

    async def _test_submit_events(self):
        jobspec = flux.job.JobspecV1.from_command(["true"])
        jobid = await flux.asyncio.job.submit(jobspec)
        self.assertGreater(jobid, 0)
        async for event in flux.asyncio.job.event_watch(jobid):
            self.assertIsInstance(event, flux.job.EventLogEvent)
            if event.name == "finish":
                self.assertTrue(os.WIFEXITED(event.context["status"]))
                self.assertEqual(0, os.WEXITSTATUS(event.context["status"]))
            if event.name == "exception":
                raise ValueError(event)


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
