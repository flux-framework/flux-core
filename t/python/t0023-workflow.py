#!/usr/bin/env python3
###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import unittest
import subflux

import flux
from flux.job.workflow import Job
from flux.job.Jobspec import JobspecV1
from flux.job import JobID

class TestJob(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

    def test_init_jobspec_obj(self):
        job = Job(self.fh, JobspecV1.from_command(["hostname"]))
        self.assertEqual(job.jobspec['tasks'][0]['command'], ["hostname"])

    def test_init_jobspec_str(self):
        job = Job(self.fh, JobspecV1.from_command(["hostname"]).dumps())
        self.assertEqual(job.jobspec['tasks'][0]['command'], ["hostname"])

    def test_init_failures(self):
        with self.assertRaises(TypeError) as error:
            Job(self.fh, 0)

    def test_submit(self):
        job = Job(self.fh, JobspecV1.from_command(["hostname"]))
        jobid = job.submit()
        self.assertGreater(jobid, 0)

    def test_submit_failures(self):
        job = Job(self.fh, JobspecV1.from_command(["hostname"]))
        jobid = job.submit()
        self.assertGreater(jobid, 0)
        with self.assertRaises(RuntimeError) as error:
            jobid = job.submit()

    def test_wait(self):
        job = Job(self.fh, JobspecV1.from_command(["hostname"]))
        jobid = job.submit(waitable=True)
        (jobid_wait, success, error) = job.wait()
        self.assertEqual(jobid, jobid_wait)
        self.assertTrue(success)

    def test_wait_failure(self):
        job = Job(self.fh, JobspecV1.from_command(["hostname"]))
        with self.assertRaises(RuntimeError) as error:
            job.wait() # haven't submitted yet
        jobid = job.submit()
        with self.assertRaises(RuntimeError) as error:
            job.wait() # not submitted with waitable=True

    def test_event_wait(self):
        job = Job(self.fh, JobspecV1.from_command(["hostname"]))
        job.submit()
        job.event_wait("depend")
        job.event_wait("alloc")
        job.event_wait("start")
        job.event_wait("finish")
        job.event_wait("free")

    def test_cancel(self):
        job = Job(self.fh, JobspecV1.from_command(["sleep", "1000"]))
        job.submit()
        job.cancel()

    def test_cancel_failure(self):
        job = Job(self.fh, JobspecV1.from_command(["sleep", "1000"]))
        with self.assertRaises(RuntimeError) as error:
            job.cancel()

    def test_kill(self):
        job = Job(self.fh, JobspecV1.from_command(["sleep", "1000"]))
        job.submit(waitable=True)

        #  Wait for shell to fully start to avoid delay in signal
        job.event_wait("start")
        job.event_wait(
            name="shell.start", eventlog="guest.exec.eventlog"
        )
        job.kill(signum=9)

        (jobid_wait, success, error) = job.wait()
        self.assertEqual(job.jobid, jobid_wait)
        self.assertFalse(success)

    def test_kill_failure(self):
        job = Job(self.fh, JobspecV1.from_command(["sleep", "1000"]))
        with self.assertRaises(RuntimeError) as error:
            job.kill()

def __flux_size():
    return 1

if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
