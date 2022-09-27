#!/usr/bin/env python3

###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest

import asyncio
import flux.job
import flux.job.JobID
import time

from subflux import rerun_under_flux


def __flux_size():
    return 1


class TestFluxAsyncio(unittest.TestCase):
    """
    Tests for Flux Asyncio.
    """

    def test_as_completed(self):
        from flux.asyncio import loop
        fluxjob = flux.job.JobspecV1.from_command(["true"])
        sleep_time = 5
        tasks = [
            loop.create_task(asyncio.sleep(sleep_time)),
            loop.create_task(flux.asyncio.submit(fluxjob)),
        ]
        asyncio.set_event_loop(loop)
        start = time.time()
        results = loop.run_until_complete(asyncio.gather(*tasks))
        end = time.time()

        # Event loop "run until complete" means we are as long as longest job
        assert end - start > sleep_time

        # Sleep result has no result
        self.assertEqual(results[0], None)

        # This is a job id
        self.assertTrue(isinstance(results[1], flux.job.JobID))

    def test_handle_equality(self):
        """
        The underlying flux handle's must be the same!
        """
        from flux.asyncio import loop
        fluxjob = flux.job.JobspecV1.from_command(["true"])
        
        # Note here we are creating a new handle
        task = loop.create_task(flux.asyncio.submit(fluxjob, flux.Flux()))
        asyncio.set_event_loop(loop)

        #  test string with invalid version
        with self.assertRaises(RuntimeError):
            loop.run_until_complete(asyncio.gather(*[task]))


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())