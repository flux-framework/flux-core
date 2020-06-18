#!/usr/bin/env python

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
import signal
from glob import glob

import yaml
import six

import flux
import flux.kvs
from flux import job
from flux.job import Jobspec, JobspecV1, ffi
from flux.future import Future


def __flux_size():
    return 1


def yaml_to_json(s):
    obj = yaml.safe_load(s)
    return json.dumps(obj, separators=(",", ":"))


class TestJob(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

        self.jobspec_dir = os.path.abspath(
            os.path.join(os.environ["FLUX_SOURCE_DIR"], "t", "jobspec")
        )

        # get a valid jobspec
        basic_jobspec_fname = os.path.join(self.jobspec_dir, "valid", "basic_v1.yaml")
        with open(basic_jobspec_fname, "rb") as infile:
            basic_yaml = infile.read()
        self.basic_jobspec = yaml_to_json(basic_yaml)

    def test_00_null_submit(self):
        with self.assertRaises(EnvironmentError) as error:
            job.submit(ffi.NULL, self.basic_jobspec)
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(EnvironmentError) as error:
            job.submit_get_id(ffi.NULL)
        self.assertEqual(error.exception.errno, errno.EINVAL)

        with self.assertRaises(EnvironmentError) as error:
            job.submit(self.fh, ffi.NULL)
        self.assertEqual(error.exception.errno, errno.EINVAL)

    def test_01_nonstring_submit(self):
        with self.assertRaises(TypeError):
            job.submit(self.fh, 0)

    def test_02_sync_submit(self):
        jobid = job.submit(self.fh, self.basic_jobspec)
        self.assertGreater(jobid, 0)

    def test_03_invalid_construction(self):
        for cls in [Jobspec, JobspecV1]:
            for invalid_jobspec_filepath in glob(
                os.path.join(self.jobspec_dir, "invalid", "*.yaml")
            ):
                with self.assertRaises(
                    (ValueError, TypeError, yaml.scanner.ScannerError)
                ) as cm:
                    cls.from_yaml_file(invalid_jobspec_filepath)

    def test_04_valid_construction(self):
        for jobspec_filepath in glob(os.path.join(self.jobspec_dir, "valid", "*.yaml")):
            Jobspec.from_yaml_file(jobspec_filepath)

    def test_05_valid_v1_construction(self):
        for jobspec_filepath in glob(
            os.path.join(self.jobspec_dir, "valid_v1", "*.yaml")
        ):
            JobspecV1.from_yaml_file(jobspec_filepath)

    def test_06_iter(self):
        jobspec_fname = os.path.join(self.jobspec_dir, "valid", "use_case_2.4.yaml")
        jobspec = Jobspec.from_yaml_file(jobspec_fname)
        self.assertEqual(len(list(jobspec)), 7)
        self.assertEqual(len(list([x for x in jobspec if x["type"] == "core"])), 2)
        self.assertEqual(len(list([x for x in jobspec if x["type"] == "slot"])), 2)

    def test_07_count(self):
        jobspec_fname = os.path.join(self.jobspec_dir, "valid", "use_case_2.4.yaml")
        jobspec = Jobspec.from_yaml_file(jobspec_fname)
        count_dict = jobspec.resource_counts()
        self.assertEqual(count_dict["node"], 1)
        self.assertEqual(count_dict["slot"], 11)
        self.assertEqual(count_dict["core"], 16)
        self.assertEqual(count_dict["memory"], 64)

    def test_08_jobspec_submit(self):
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        jobid = job.submit(self.fh, jobspec)
        self.assertGreater(jobid, 0)

    def test_09_valid_duration(self):
        """Test setting Jobspec duration to various valid values"""
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        for duration in (100, 100.5):
            delta = datetime.timedelta(seconds=duration)
            for x in [duration, delta, "{}s".format(duration)]:
                jobspec.duration = x
                # duration setter converts value to a float
                self.assertEqual(jobspec.duration, float(duration))

    def test_10_invalid_duration(self):
        """Test setting Jobspec duration to various invalid values and types"""
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        for duration in (-100, -100.5, datetime.timedelta(seconds=-5), "10h5m"):
            with self.assertRaises(ValueError):
                jobspec.duration = duration
        for duration in ([], {}):
            with self.assertRaises(TypeError):
                jobspec.duration = duration

    def test_11_cwd_pathlib(self):
        if six.PY2:
            return
        import pathlib

        jobspec_path = pathlib.PosixPath(self.jobspec_dir) / "valid" / "basic_v1.yaml"
        jobspec = Jobspec.from_yaml_file(jobspec_path)
        cwd = pathlib.PosixPath("/tmp")
        jobspec.cwd = cwd
        self.assertEqual(jobspec.cwd, os.fspath(cwd))

    def test_12_environment(self):
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        new_env = {"HOME": "foo", "foo": "bar"}
        jobspec.environment = new_env
        self.assertEqual(jobspec.environment, new_env)

    def test_13_job_kvs(self):
        jobid = job.submit(self.fh, self.basic_jobspec, waitable=True)
        job.wait(self.fh, jobid=jobid)
        for job_kvs_dir in [
            job.job_kvs(self.fh, jobid),
            job.job_kvs_guest(self.fh, jobid),
        ]:
            self.assertTrue(isinstance(job_kvs_dir, flux.kvs.KVSDir))
            self.assertTrue(flux.kvs.exists(self.fh, job_kvs_dir.path))
            self.assertTrue(flux.kvs.isdir(self.fh, job_kvs_dir.path))

    def test_14_job_cancel_invalid_args(self):
        with self.assertRaises(ValueError):
            job.kill(self.fh, "abc")
        with self.assertRaises(ValueError):
            job.cancel(self.fh, "abc")
        with self.assertRaises(OSError):
            job.kill(self.fh, 123)
        with self.assertRaises(OSError):
            job.cancel(self.fh, 123)

    def test_15_job_cancel(self):
        self.sleep_jobspec = JobspecV1.from_command(["sleep", "1000"])
        jobid = job.submit(self.fh, self.sleep_jobspec, waitable=True)
        job.cancel(self.fh, jobid)
        fut = job.wait_async(self.fh, jobid=jobid).wait_for(5.0)
        return_id, success, errmsg = fut.get_status()
        self.assertEqual(return_id, jobid)
        self.assertFalse(success)

    def test_16_job_kill(self):
        self.sleep_jobspec = JobspecV1.from_command(["sleep", "1000"])
        jobid = job.submit(self.fh, self.sleep_jobspec, waitable=True)

        #  Wait for shell to fully start to avoid delay in signal
        job.event_wait(self.fh, jobid, name="start")
        job.event_wait(
            self.fh, jobid, name="shell.start", eventlog="guest.exec.eventlog"
        )
        job.kill(self.fh, jobid, signum=signal.SIGKILL)
        fut = job.wait_async(self.fh, jobid=jobid).wait_for(5.0)
        return_id, success, errmsg = fut.get_status()
        self.assertEqual(return_id, jobid)
        self.assertFalse(success)

    def test_20_001_job_event_functions_invalid_args(self):
        with self.assertRaises(OSError) as cm:
            for event in job.event_watch(self.fh, 123):
                print(event)
        self.assertEqual(cm.exception.errno, errno.ENOENT)
        with self.assertRaises(OSError) as cm:
            job.event_wait(self.fh, 123, "start")
        self.assertEqual(cm.exception.errno, errno.ENOENT)
        with self.assertRaises(OSError) as cm:
            job.event_wait(None, 123, "start")
        self.assertEqual(cm.exception.errno, errno.EINVAL)

    def test_20_001_job_event_watch_async(self):
        myarg = dict(a=1, b=2)
        events = []

        def cb(future, arg):
            self.assertEqual(arg, myarg)
            event = future.get_event()
            if event is None:
                future.get_flux().reactor_stop()
                return
            self.assertIsInstance(event, job.EventLogEvent)
            events.append(event.name)

        jobid = job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))
        self.assertTrue(jobid > 0)
        future = job.event_watch_async(self.fh, jobid)
        self.assertIsInstance(future, job.JobEventWatchFuture)
        future.then(cb, myarg)
        rc = self.fh.reactor_run()
        self.assertGreaterEqual(rc, 0)
        self.assertEqual(len(events), 8)
        self.assertEqual(events[0], "submit")
        self.assertEqual(events[-1], "clean")

    def test_20_002_job_event_watch_no_autoreset(self):
        jobid = job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))
        self.assertTrue(jobid > 0)
        future = job.event_watch_async(self.fh, jobid)
        self.assertIsInstance(future, job.JobEventWatchFuture)

        # First event should be "submit"
        event = future.get_event(autoreset=False)
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEqual(event.name, "submit")

        # get_event() again with no reset returns same event:
        event = future.get_event(autoreset=False)
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEqual(event.name, "submit")

        # reset, then get_event() should get next event
        future.reset()
        event = future.get_event(autoreset=False)
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEqual(event.name, "depend")

        future.cancel()

    def test_20_003_job_event_watch_sync(self):
        jobid = job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))
        self.assertTrue(jobid > 0)
        future = job.event_watch_async(self.fh, jobid)
        self.assertIsInstance(future, job.JobEventWatchFuture)
        event = future.get_event()
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEquals(event.name, "submit")
        future.cancel()

    def test_20_004_job_event_watch(self):
        jobid = job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))
        self.assertTrue(jobid > 0)
        events = []
        for event in job.event_watch(self.fh, jobid):
            self.assertIsInstance(event, job.EventLogEvent)
            self.assertTrue(hasattr(event, "timestamp"))
            self.assertTrue(hasattr(event, "name"))
            self.assertTrue(hasattr(event, "context"))
            self.assertIs(type(event.timestamp), float)
            self.assertIs(type(event.name), str)
            self.assertIs(type(event.context), dict)
            events.append(event.name)
        self.assertEqual(len(events), 8)

    def test_20_005_job_event_watch_with_cancel(self):
        jobid = job.submit(
            self.fh, JobspecV1.from_command(["sleep", "3"]), waitable=True
        )
        self.assertTrue(jobid > 0)
        events = []
        future = job.event_watch_async(self.fh, jobid)
        while True:
            event = future.get_event()
            if event is None:
                break
            if event.name == "start":
                future.cancel()
            events.append(event.name)
        self.assertEqual(event, None)
        # Should have less than the expected number of events due to cancel
        self.assertLess(len(events), 8)
        job.cancel(self.fh, jobid)
        job.wait(self.fh, jobid)

    def test_20_006_job_event_wait(self):
        jobid = job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))
        self.assertTrue(jobid > 0)
        event = job.event_wait(self.fh, jobid, "start")
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEqual(event.name, "start")
        event = job.event_wait(
            self.fh, jobid, "shell.init", eventlog="guest.exec.eventlog"
        )
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEqual(event.name, "shell.init")
        event = job.event_wait(self.fh, jobid, "clean")
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEqual(event.name, "clean")
        with self.assertRaises(OSError):
            job.event_wait(self.fh, jobid, "foo")


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
