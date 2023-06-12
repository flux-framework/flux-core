#!/usr/bin/env python3

###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import unittest

import flux
import subflux  # noqa: F401 - for PYTHONPATH
from flux.job import JobspecV1


def __flux_size():
    return 1


class TestJob(unittest.TestCase):
    @classmethod
    def submitJob(self, command):
        compute_jobreq = JobspecV1.from_command(
            command=command, num_tasks=1, num_nodes=1, cores_per_task=1
        )
        return flux.job.submit(self.fh, compute_jobreq, waitable=True)

    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()
        self.jobid1 = self.submitJob(["hostname"])
        flux.job.event_wait(self.fh, self.jobid1, name="start")
        self.jobid2 = self.submitJob(["hostname"])
        flux.job.event_wait(self.fh, self.jobid2, name="start")

    def check_jobspec_str(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(type(data["jobspec"]), str)
        jobspec = json.loads(data["jobspec"])
        self.assertEqual(jobspec["tasks"][0]["command"][0], "hostname")
        self.assertNotIn("R", data)

    def check_jobspec_decoded(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(data["jobspec"]["tasks"][0]["command"][0], "hostname")
        self.assertNotIn("R", data)

    def check_R_J_str(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertNotIn("jobspec", data, jobid)
        self.assertIn("R", data)
        self.assertIn("J", data)
        self.assertEqual(type(data["R"]), str)
        self.assertEqual(type(data["J"]), str)
        R = json.loads(data["R"])
        self.assertEqual(R["execution"]["R_lite"][0]["rank"], "0")

    def check_R_J_decoded(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertNotIn("jobspec", data)
        self.assertIn("R", data)
        self.assertIn("J", data)
        self.assertEqual(type(data["J"]), str)
        self.assertEqual(data["R"]["execution"]["R_lite"][0]["rank"], "0")

    def test_00_job_info_lookup(self):
        rpc = flux.job.job_info_lookup(self.fh, self.jobid1)
        data = rpc.get()
        self.check_jobspec_str(data, self.jobid1)
        data = rpc.get_decode()
        self.assertEqual(data["id"], self.jobid1)

    def test_01_job_info_lookup_keys(self):
        rpc = flux.job.job_info_lookup(self.fh, self.jobid1, keys=["R", "J"])
        data = rpc.get()
        self.check_R_J_str(data, self.jobid1)
        data = rpc.get_decode()
        self.check_R_J_decoded(data, self.jobid1)

    def test_02_job_info_lookup_badid(self):
        rpc = flux.job.job_info_lookup(self.fh, 123456789)
        notfound = False
        try:
            rpc.get()
        except FileNotFoundError:
            notfound = True
        self.assertEqual(notfound, True)

    def test_03_job_info_lookup_badkey(self):
        rpc = flux.job.job_info_lookup(self.fh, self.jobid1, keys=["foo"])
        notfound = False
        try:
            rpc.get()
        except FileNotFoundError:
            notfound = True
        self.assertEqual(notfound, True)

    def test_04_job_kvs_lookup(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1)
        self.check_jobspec_decoded(data, self.jobid1)

    def test_05_job_kvs_lookup_nodecode(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, decode=False)
        self.check_jobspec_str(data, self.jobid1)

    def test_06_job_kvs_lookup_keys(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["R", "J"])
        self.check_R_J_decoded(data, self.jobid1)

    def test_07_job_kvs_lookup_keys_nodecode(self):
        data = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["R", "J"], decode=False
        )
        self.check_R_J_str(data, self.jobid1)

    def test_08_job_kvs_lookup_badid(self):
        data = flux.job.job_kvs_lookup(self.fh, 123456789)
        self.assertEqual(data, None)

    def test_09_job_kvs_lookup_badkey(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["foo"])
        self.assertEqual(data, None)

    def test_10_job_kvs_lookup_list(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(self.fh, ids).data()
        self.assertEqual(len(data), 1)
        self.check_jobspec_decoded(data[0], self.jobid1)

    def test_11_job_kvs_lookup_list_multiple(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids).data()
        self.assertEqual(len(data), 2)
        self.check_jobspec_decoded(data[0], self.jobid1)
        self.check_jobspec_decoded(data[1], self.jobid2)

    def test_12_job_kvs_lookup_list_multiple_nodecode(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids, decode=False).data()
        self.assertEqual(len(data), 2)
        self.check_jobspec_str(data[0], self.jobid1)
        self.check_jobspec_str(data[1], self.jobid2)

    def test_13_job_kvs_lookup_list_multiple_keys(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["R", "J"]).data()
        self.assertEqual(len(data), 2)
        self.check_R_J_decoded(data[0], self.jobid1)
        self.check_R_J_decoded(data[1], self.jobid2)

    def test_14_job_kvs_lookup_list_multiple_keys_nodecode(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["R", "J"], decode=False).data()
        self.assertEqual(len(data), 2)
        self.check_R_J_str(data[0], self.jobid1)
        self.check_R_J_str(data[1], self.jobid2)

    def test_15_job_kvs_lookup_list_none(self):
        data = flux.job.JobKVSLookup(self.fh).data()
        self.assertEqual(len(data), 0)

    def test_16_job_kvs_lookup_list_badid(self):
        ids = [123456789]
        datalookup = flux.job.JobKVSLookup(self.fh, ids)
        data = datalookup.data()
        self.assertEqual(len(data), 0)
        self.assertEqual(len(datalookup.errors), 1)

    def test_17_job_kvs_lookup_list_badkey(self):
        ids = [self.jobid1]
        datalookup = flux.job.JobKVSLookup(self.fh, ids, keys=["foo"])
        data = datalookup.data()
        self.assertEqual(len(data), 0)
        self.assertEqual(len(datalookup.errors), 1)


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
