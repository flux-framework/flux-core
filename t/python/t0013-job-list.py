#!/usr/bin/env python3

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
import errno
import sys
import json
import unittest
import datetime
import time
from glob import glob

import yaml

import flux
from flux import job
from flux.job import Jobspec, JobspecV1, ffi


def __flux_size():
    return 1


class TestJob(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

    @classmethod
    def submitJob(self, command=None):
        if not command:
            command = ["sleep", "0"]
        compute_jobreq = JobspecV1.from_command(
            command=command, num_tasks=2, num_nodes=1, cores_per_task=1
        )
        compute_jobreq.cwd = os.getcwd()
        compute_jobreq.environment = dict(os.environ)
        return flux.job.submit(self.fh, compute_jobreq, waitable=True)

    @classmethod
    def getJobs(self, rpc_handle):
        try:
            jobs = rpc_handle.get_jobs()
            return jobs
        except EnvironmentError as e:
            print("{}: {}".format("rpc", e.strerror), file=sys.stderr)
            sys.exit(1)

    # NOTE: the job-list module has eventual consistency with the jobs stored
    # in the job-manager's queue. To ensure no raciness in tests, we spin
    # until all of the inactive jobs have reached INACTIVE state.
    @classmethod
    def waitForConsistency(self, jobs_list_length):
        jobs = []
        waiting = 0  # number of seconds we have been waiting
        while True:
            rpc_handle = flux.job.job_list(
                self.fh, 0, states=flux.constants.FLUX_JOB_STATE_INACTIVE
            )
            jobs = self.getJobs(rpc_handle)
            if len(jobs) >= jobs_list_length:
                break
            time.sleep(1)
            waiting += 1
            if waiting > 60:
                raise TimeoutError()

    # flux job list should return an empty list if there are no jobs
    def test_00_list_expect_empty_list(self):
        rpc_handle = flux.job.job_list(self.fh)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 0)

    # flux job list-inactive should return an empty list if there are
    # no inactive jobs
    def test_01_list_inactive_expect_empty_list(self):
        rpc_handle = flux.job.job_list_inactive(self.fh, time.time() - 3600, 10)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 0)

    # flux job list make sure one job is read from RPC
    def test_02_list_success(self):
        # submit a sleep 0 job
        self.submitJob()

        self.waitForConsistency(1)

        rpc_handle = flux.job.job_list(self.fh)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 1)

    # flux job list-inactive make sure one job is read from RPC
    def test_03_list_inactive_success(self):
        rpc_handle = flux.job.job_list_inactive(self.fh, time.time() - 3600, 10)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 1)

    # flux job list multiple jobs submitted should return a
    # longer list of inactive jobs
    def test_04_list_multiple_inactive(self):
        # submit a bundle of sleep 0 jobs
        for i in range(10):
            jobid = self.submitJob()

        # 11 = 10 + 1 in previous tests
        self.waitForConsistency(11)

        rpc_handle = flux.job.job_list(self.fh)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 11)

    # flux job list-inactive multiple jobs submitted should return a
    # longer list of inactive jobs
    def test_05_list_inactive_multiple_inactive(self):
        rpc_handle = flux.job.job_list_inactive(self.fh, time.time() - 3600, 20)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 11)

    # flux job list-inactive with since = 0.0 should return all inactive jobs
    def test_06_list_inactive_all(self):
        rpc_handle = flux.job.job_list_inactive(self.fh, 0.0, 20)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 11)

    # flux job list-inactive with max_entries = 5 should only return a subset
    def test_07_list_inactive_subset_of_inactive(self):
        rpc_handle = flux.job.job_list_inactive(self.fh, 0.0, 5)

        jobs = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs), 5)

    # flux job list-inactive with the most recent timestamp should return len(0)
    def test_08_list_inactive_most_recent_inactive(self):
        rpc_handle = flux.job.job_list(
            self.fh, 1, ["t_inactive"], states=flux.constants.FLUX_JOB_STATE_INACTIVE
        )

        jobs = self.getJobs(rpc_handle)

        rpc_handle = flux.job.job_list_inactive(self.fh, jobs[0]["t_inactive"], 1)

        jobs_inactive = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs_inactive), 0)

    # flux job list-inactive with second to most recent timestamp
    def test_09_list_inactive_second_most_recent_timestamp(self):
        rpc_handle = flux.job.job_list(
            self.fh, 2, ["t_inactive"], states=flux.constants.FLUX_JOB_STATE_INACTIVE
        )

        jobs = self.getJobs(rpc_handle)

        rpc_handle = flux.job.job_list_inactive(self.fh, jobs[1]["t_inactive"], 1)

        jobs_inactive = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs_inactive), 1)
        self.assertEqual(jobs_inactive[0]["t_inactive"], jobs[0]["t_inactive"])

    # flux job list-inactive with oldest timestamp
    def test_10_list_inactive_oldest_timestamp(self):
        rpc_handle = flux.job.job_list(
            self.fh, 5, ["t_inactive"], states=flux.constants.FLUX_JOB_STATE_INACTIVE
        )

        jobs = self.getJobs(rpc_handle)

        rpc_handle = flux.job.job_list_inactive(self.fh, jobs[4]["t_inactive"], 10)

        jobs_inactive = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs_inactive), 4)

    # flux job list-inactive with middle timestamp #1
    def test_11_list_inactive_middle_timestamp_1(self):
        rpc_handle = flux.job.job_list(
            self.fh, 20, ["t_inactive"], states=flux.constants.FLUX_JOB_STATE_INACTIVE
        )

        jobs = self.getJobs(rpc_handle)

        rpc_handle = flux.job.job_list_inactive(self.fh, jobs[5]["t_inactive"], 20)

        jobs_inactive = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs_inactive), 5)

    # flux job list-inactive with middle timestamp #2
    def test_12_list_inactive_middle_timestamp_2(self):
        rpc_handle = flux.job.job_list(
            self.fh, 20, ["t_inactive"], states=flux.constants.FLUX_JOB_STATE_INACTIVE
        )

        jobs = self.getJobs(rpc_handle)

        rpc_handle = flux.job.job_list_inactive(self.fh, jobs[7]["t_inactive"], 20)

        jobs_inactive = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs_inactive), 7)

    # flux job list-inactive with name filter
    def test_13_list_inactive_name_filter(self):
        # submit a bundle of hostname jobs
        for i in range(5):
            jobid = self.submitJob(["hostname"])

        # 16 = 5 + 11 in previous tests
        self.waitForConsistency(16)

        rpc_handle = flux.job.job_list_inactive(self.fh, 0.0, 20, name="sleep")

        jobs_inactive = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs_inactive), 11)

        rpc_handle = flux.job.job_list_inactive(self.fh, 0.0, 20, name="hostname")

        jobs_inactive = self.getJobs(rpc_handle)

        self.assertEqual(len(jobs_inactive), 5)

    # flux job list-id works
    def test_14_list_id(self):
        jobid = self.submitJob(["hostname"])

        self.waitForConsistency(17)

        rpc_handle = flux.job.job_list_id(self.fh, jobid)

        job = rpc_handle.get_job()
        self.assertEqual(job["id"], jobid)

    # flux job list-id fails on bad id
    def test_15_list_id_fail(self):
        rpc_handle = flux.job.job_list_id(self.fh, 123456789)
        notfound = False

        try:
            jobinfo = rpc_handle.get_jobinfo()
        except FileNotFoundError:
            notfound = True

        self.assertEqual(notfound, True)


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
