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

import errno
import json
import os
import unittest

import flux
import subflux  # noqa: F401 - for PYTHONPATH
from flux.job import JobspecV1


def __flux_size():
    return 1


class TestJob(unittest.TestCase):
    @classmethod
    def submitJob(self, command, urgency):
        compute_jobreq = JobspecV1.from_command(
            command=command, num_tasks=1, num_nodes=1, cores_per_task=1
        )
        testenv = {"FOO": "BAR"}
        compute_jobreq.environment = testenv
        return flux.job.submit(self.fh, compute_jobreq, urgency=urgency, waitable=True)

    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

        # in future use more standard update mechanism instead of
        # jobtap plugin.  This jobtap plugin will simply increase the
        # job expiration by 60 minutes.
        pluginpath = (
            os.environ["FLUX_BUILD_DIR"]
            + "/t/job-manager/plugins/.libs/resource-update-expiration.so"
        )
        payload = {"load": pluginpath}
        self.fh.rpc("job-manager.jobtap", payload).get()

        self.jobid1 = self.submitJob(["hostname"], 0)
        flux.job.event_wait(self.fh, self.jobid1, name="priority")
        update = {"attributes.system.duration": 100.0}
        payload = {"id": self.jobid1, "updates": update}
        self.fh.rpc("job-manager.update", payload).get()
        payload = {"id": self.jobid1, "urgency": 16}
        self.fh.rpc("job-manager.urgency", payload).get()
        flux.job.event_wait(self.fh, self.jobid1, name="clean")

        payload = {"remove": "all"}
        self.fh.rpc("job-manager.jobtap", payload).get()

        self.jobid2 = self.submitJob(["hostname"], 16)
        flux.job.event_wait(self.fh, self.jobid2, name="clean")

    def check_jobspec_str(self, data, jobid, duration):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(type(data["jobspec"]), str)
        jobspec = json.loads(data["jobspec"])
        self.assertEqual(jobspec["tasks"][0]["command"][0], "hostname")
        self.assertEqual(jobspec["attributes"]["system"]["duration"], duration)
        self.assertNotIn("R", data)

    def check_jobspec_decoded(self, data, jobid, duration):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(data["jobspec"]["tasks"][0]["command"][0], "hostname")
        self.assertEqual(data["jobspec"]["attributes"]["system"]["duration"], duration)
        self.assertNotIn("R", data)

    def check_R_str(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("R", data)
        self.assertEqual(type(data["R"]), str)
        R = json.loads(data["R"])
        self.assertEqual(R["execution"]["R_lite"][0]["rank"], "0")

    def check_R_decoded(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("R", data)
        self.assertEqual(data["R"]["execution"]["R_lite"][0]["rank"], "0")

    def check_J_str(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("J", data)
        self.assertEqual(type(data["J"]), str)

    def check_J_decoded(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("J", data)
        self.assertEqual(type(data["J"]), str)

    def check_jobspec_original_str(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(type(data["jobspec"]), str)
        jobspec = json.loads(data["jobspec"])
        self.assertEqual(jobspec["tasks"][0]["command"][0], "hostname")
        self.assertEqual(jobspec["attributes"]["system"]["duration"], 0)
        self.assertEqual(jobspec["attributes"]["system"]["environment"]["FOO"], "BAR")

    def check_jobspec_original_decoded(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(data["jobspec"]["tasks"][0]["command"][0], "hostname")
        self.assertEqual(data["jobspec"]["attributes"]["system"]["duration"], 0)
        self.assertEqual(
            data["jobspec"]["attributes"]["system"]["environment"]["FOO"], "BAR"
        )

    def check_jobspec_base_str(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(type(data["jobspec"]), str)
        jobspec = json.loads(data["jobspec"])
        self.assertEqual(jobspec["tasks"][0]["command"][0], "hostname")
        self.assertEqual(jobspec["attributes"]["system"]["duration"], 0)

    def check_jobspec_base_decoded(self, data, jobid):
        self.assertEqual(data["id"], jobid)
        self.assertIn("jobspec", data)
        self.assertEqual(data["jobspec"]["tasks"][0]["command"][0], "hostname")
        self.assertEqual(data["jobspec"]["attributes"]["system"]["duration"], 0)

    def check_R_base_str(self, jobid, base, data):
        self.assertEqual(base["id"], jobid)
        self.assertEqual(data["id"], jobid)
        self.assertIn("R", base)
        self.assertIn("R", data)
        self.assertEqual(type(base["R"]), str)
        self.assertEqual(type(data["R"]), str)
        R_base = json.loads(base["R"])
        R_data = json.loads(data["R"])
        base_expiration = R_base["execution"]["expiration"]
        data_expiration = R_data["execution"]["expiration"]
        self.assertGreater(data_expiration, base_expiration)

    def check_R_base_decoded(self, jobid, base, data):
        self.assertEqual(base["id"], jobid)
        self.assertEqual(data["id"], jobid)
        self.assertIn("R", base)
        self.assertIn("R", data)
        base_expiration = base["R"]["execution"]["expiration"]
        data_expiration = data["R"]["execution"]["expiration"]
        self.assertGreater(data_expiration, base_expiration)

    def test_info_00_job_info_lookup(self):
        rpc = flux.job.job_info_lookup(self.fh, self.jobid1)
        data = rpc.get()
        self.check_jobspec_str(data, self.jobid1, 0)
        data = rpc.get_decode()
        self.check_jobspec_decoded(data, self.jobid1, 0)
        self.assertEqual(data["id"], self.jobid1, 0)

    def test_info_01_job_info_lookup_keys(self):
        rpc = flux.job.job_info_lookup(self.fh, self.jobid1, keys=["R", "J"])
        data = rpc.get()
        self.assertNotIn("jobspec", data)
        self.check_R_str(data, self.jobid1)
        self.check_J_str(data, self.jobid1)
        data = rpc.get_decode()
        self.check_R_decoded(data, self.jobid1)
        self.check_J_decoded(data, self.jobid1)

    def test_info_02_job_info_lookup_badid(self):
        rpc = flux.job.job_info_lookup(self.fh, 123456789)
        with self.assertRaises(FileNotFoundError):
            rpc.get()

    def test_info_03_job_info_lookup_badkey(self):
        rpc = flux.job.job_info_lookup(self.fh, self.jobid1, keys=["foo"])
        with self.assertRaises(FileNotFoundError):
            rpc.get()

    def test_info_00_job_info_update_lookup(self):
        rpc = flux.job.job_info_update_lookup(self.fh, self.jobid1)
        data = rpc.get()
        self.check_jobspec_str(data, self.jobid1, 100.0)
        data = rpc.get_decode()
        self.check_jobspec_decoded(data, self.jobid1, 100.0)
        self.assertEqual(data["id"], self.jobid1)

    def test_info_01_job_info_update_lookup_badid(self):
        rpc = flux.job.job_info_update_lookup(self.fh, 123456789)
        with self.assertRaises(FileNotFoundError):
            rpc.get()

    def test_info_02_job_info_update_lookup_badkey(self):
        rpc = flux.job.job_info_update_lookup(self.fh, self.jobid1, key="foo")
        with self.assertRaises(OSError) as e:
            rpc.get()
        self.assertEqual(e.exception.errno, errno.EINVAL)

    def test_lookup_01_job_kvs_lookup(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1)
        self.check_jobspec_decoded(data, self.jobid1, 100.0)

    def test_lookup_02_job_kvs_lookup_nodecode(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, decode=False)
        self.check_jobspec_str(data, self.jobid1, 100.0)

    def test_lookup_03_job_kvs_lookup_keys(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["R", "J"])
        self.assertNotIn("jobspec", data)
        self.check_R_decoded(data, self.jobid1)
        self.check_J_decoded(data, self.jobid1)

    def test_lookup_04_job_kvs_lookup_keys_nodecode(self):
        data = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["R", "J"], decode=False
        )
        self.assertNotIn("jobspec", data)
        self.check_R_str(data, self.jobid1)
        self.check_J_str(data, self.jobid1)

    def test_lookup_05_job_kvs_lookup_badid(self):
        data = flux.job.job_kvs_lookup(self.fh, 123456789)
        self.assertEqual(data, None)

    def test_lookup_06_job_kvs_lookup_badkey(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["foo"])
        self.assertEqual(data, None)

    def test_lookup_07_job_kvs_lookup_jobspec_original(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, original=True)
        self.assertNotIn("J", data)
        self.check_jobspec_original_decoded(data, self.jobid1)

    def test_lookup_08_job_kvs_lookup_jobspec_original_nodecode(self):
        data = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, decode=False, original=True
        )
        self.assertNotIn("J", data)
        self.check_jobspec_original_str(data, self.jobid1)

    def test_lookup_09_job_kvs_lookup_jobspec_original_multiple_keys(self):
        data = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["jobspec", "J"], original=True
        )
        self.assertIn("J", data)
        self.check_jobspec_original_decoded(data, self.jobid1)

    def test_lookup_10_job_kvs_lookup_original_no_jobspec(self):
        data = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["R", "J"], original=True
        )
        self.assertNotIn("jobspec", data)
        self.check_R_decoded(data, self.jobid1)
        self.check_J_decoded(data, self.jobid1)

    def test_lookup_11_job_kvs_lookup_jobspec_base(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, base=True)
        self.assertNotIn("eventlog", data)
        self.check_jobspec_base_decoded(data, self.jobid1)

    def test_lookup_12_job_kvs_lookup_jobspec_base_nodecode(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, decode=False, base=True)
        self.assertNotIn("eventlog", data)
        self.check_jobspec_base_str(data, self.jobid1)

    def test_lookup_13_job_kvs_lookup_jobspec_base_multiple_keys(self):
        data = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["jobspec", "eventlog"], base=True
        )
        self.assertIn("eventlog", data)
        self.check_jobspec_base_decoded(data, self.jobid1)

    def test_lookup_14_job_kvs_lookup_R_base(self):
        base = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["R"], base=True)
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["R"])
        self.assertNotIn("eventlog", base)
        self.assertNotIn("eventlog", data)
        self.check_R_base_decoded(self.jobid1, base, data)

    def test_lookup_15_job_kvs_lookup_R_base_nodecode(self):
        base = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["R"], decode=False, base=True
        )
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["R"], decode=False)
        self.assertNotIn("eventlog", base)
        self.assertNotIn("eventlog", data)
        self.check_R_base_str(self.jobid1, base, data)

    def test_lookup_16_job_kvs_lookup_R_base_multiple_keys(self):
        base = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["R", "eventlog"], decode=False, base=True
        )
        data = flux.job.job_kvs_lookup(
            self.fh, self.jobid1, keys=["R", "eventlog"], decode=False
        )
        self.assertIn("eventlog", base)
        self.assertIn("eventlog", data)
        self.check_R_base_str(self.jobid1, base, data)

    def test_lookup_17_job_kvs_lookup_base_no_jobspec_R(self):
        data = flux.job.job_kvs_lookup(self.fh, self.jobid1, keys=["J"], base=True)
        self.assertNotIn("jobspec", data)
        self.assertNotIn("R", data)
        self.check_J_decoded(data, self.jobid1)

    def test_list_00_job_kvs_lookup_list(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(self.fh, ids).data()
        self.assertEqual(len(data), 1)
        self.check_jobspec_decoded(data[0], self.jobid1, 100.0)

    def test_list_01_job_kvs_lookup_list_multiple(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids).data()
        self.assertEqual(len(data), 2)
        self.check_jobspec_decoded(data[0], self.jobid1, 100.0)
        self.check_jobspec_decoded(data[1], self.jobid2, 0)

    def test_list_02_job_kvs_lookup_list_multiple_nodecode(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids, decode=False).data()
        self.assertEqual(len(data), 2)
        self.check_jobspec_str(data[0], self.jobid1, 100.0)
        self.check_jobspec_str(data[1], self.jobid2, 0)

    def test_list_03_job_kvs_lookup_list_multiple_keys(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["R", "J"]).data()
        self.assertNotIn("jobspec", data)
        self.assertEqual(len(data), 2)
        self.check_R_decoded(data[0], self.jobid1)
        self.check_J_decoded(data[0], self.jobid1)
        self.check_R_decoded(data[1], self.jobid2)
        self.check_J_decoded(data[1], self.jobid2)

    def test_list_04_job_kvs_lookup_list_multiple_keys_nodecode(self):
        ids = [self.jobid1, self.jobid2]
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["R", "J"], decode=False).data()
        self.assertNotIn("jobspec", data)
        self.assertEqual(len(data), 2)
        self.check_R_str(data[0], self.jobid1)
        self.check_J_str(data[0], self.jobid1)
        self.check_R_str(data[1], self.jobid2)
        self.check_J_str(data[1], self.jobid2)

    def test_list_05_job_kvs_lookup_list_none(self):
        data = flux.job.JobKVSLookup(self.fh).data()
        self.assertEqual(len(data), 0)

    def test_list_06_job_kvs_lookup_list_badid(self):
        ids = [123456789]
        datalookup = flux.job.JobKVSLookup(self.fh, ids)
        data = datalookup.data()
        self.assertEqual(len(data), 0)
        self.assertEqual(len(datalookup.errors), 1)

    def test_list_07_job_kvs_lookup_list_badkey(self):
        ids = [self.jobid1]
        datalookup = flux.job.JobKVSLookup(self.fh, ids, keys=["foo"])
        data = datalookup.data()
        self.assertEqual(len(data), 0)
        self.assertEqual(len(datalookup.errors), 1)

    def test_list_08_job_kvs_lookup_list_jobspec_original(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(self.fh, ids, original=True).data()
        self.assertEqual(len(data), 1)
        self.assertNotIn("J", data[0])
        self.check_jobspec_original_decoded(data[0], self.jobid1)

    def test_list_09_job_kvs_lookup_list_jobspec_original_nodecode(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(self.fh, ids, decode=False, original=True).data()
        self.assertEqual(len(data), 1)
        self.assertNotIn("J", data[0])
        self.check_jobspec_original_str(data[0], self.jobid1)

    def test_list_10_job_kvs_lookup_list_jobspec_original_multiple_keys(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(
            self.fh, ids, keys=["jobspec", "J"], original=True
        ).data()
        self.assertEqual(len(data), 1)
        self.assertIn("J", data[0])
        self.check_jobspec_original_decoded(data[0], self.jobid1)

    def test_list_11_job_kvs_lookup_list_original_no_jobspec(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(
            self.fh, ids, keys=["R", "J"], original=True
        ).data()
        self.assertEqual(len(data), 1)
        self.assertNotIn("jobspec", data[0])
        self.check_R_decoded(data[0], self.jobid1)
        self.check_J_decoded(data[0], self.jobid1)

    def test_list_12_job_kvs_lookup_list_jobspec_base(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(self.fh, ids, base=True).data()
        self.assertEqual(len(data), 1)
        self.assertNotIn("J", data[0])
        self.check_jobspec_base_decoded(data[0], self.jobid1)

    def test_list_13_job_kvs_lookup_list_jobspec_base_nodecode(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(self.fh, ids, decode=False, base=True).data()
        self.assertEqual(len(data), 1)
        self.assertNotIn("J", data[0])
        self.check_jobspec_base_str(data[0], self.jobid1)

    def test_list_14_job_kvs_lookup_list_jobspec_base_multiple_keys(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(
            self.fh, ids, keys=["jobspec", "J"], base=True
        ).data()
        self.assertEqual(len(data), 1)
        self.assertIn("J", data[0])
        self.check_jobspec_base_decoded(data[0], self.jobid1)

    def test_list_15_job_kvs_lookup_list_R_base(self):
        ids = [self.jobid1]
        base = flux.job.JobKVSLookup(self.fh, ids, keys=["R"], base=True).data()
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["R"]).data()
        self.assertEqual(len(base), 1)
        self.assertEqual(len(data), 1)
        self.check_R_base_decoded(self.jobid1, base[0], data[0])

    def test_list_16_job_kvs_lookup_list_R_base_nodecode(self):
        ids = [self.jobid1]
        base = flux.job.JobKVSLookup(
            self.fh, ids, keys=["R"], decode=False, base=True
        ).data()
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["R"], decode=False).data()
        self.assertEqual(len(base), 1)
        self.assertEqual(len(data), 1)
        self.check_R_base_str(self.jobid1, base[0], data[0])

    def test_list_17_job_kvs_lookup_list_R_base_multiple_keys(self):
        ids = [self.jobid1]
        base = flux.job.JobKVSLookup(self.fh, ids, keys=["R", "J"], base=True).data()
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["R", "J"]).data()
        self.assertEqual(len(data), 1)
        self.assertIn("J", data[0])
        self.check_R_base_decoded(self.jobid1, base[0], data[0])

    def test_list_18_job_kvs_lookup_list_base_no_jobspec_R(self):
        ids = [self.jobid1]
        data = flux.job.JobKVSLookup(self.fh, ids, keys=["J"], base=True).data()
        self.assertEqual(len(data), 1)
        self.assertNotIn("jobspec", data[0])
        self.assertNotIn("R", data[0])
        self.check_J_decoded(data[0], self.jobid1)


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
