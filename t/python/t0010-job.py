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

import datetime
import errno
import json
import locale
import os
import pathlib
import signal
import subprocess
import unittest
from glob import glob

import flux
import flux.constants
import flux.kvs
import yaml
from flux import job
from flux.job import JobInfo, Jobspec, JobspecV1, ffi
from flux.job.stats import JobStats


def __flux_size():
    return 1


def yaml_to_json(s):
    obj = yaml.safe_load(s)
    return json.dumps(obj, separators=(",", ":"))


class TestJob(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        os.unsetenv("FLUX_F58_FORCE_ASCII")
        self.fh = flux.Flux()
        self.use_ascii = False
        build_opts = subprocess.check_output(["flux", "version"]).decode()
        if locale.getlocale()[1] != "UTF-8" or "ascii-only" in build_opts:
            self.use_ascii = True

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
                ):
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

    def test_12_0_queue(self):
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        jobspec.queue = "default"
        self.assertEqual(jobspec.queue, "default")

    def test_12_1_queue_invalid(self):
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        with self.assertRaises(TypeError):
            jobspec.queue = 12

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

    def test_20_000_job_event_functions_invalid_args(self):
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
        self.assertEqual(len(events), 10)
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
        self.assertEqual(event.name, "validate")

        future.cancel()

    def test_20_003_job_event_watch_sync(self):
        jobid = job.submit(self.fh, JobspecV1.from_command(["sleep", "0"]))
        self.assertTrue(jobid > 0)
        future = job.event_watch_async(self.fh, jobid)
        self.assertIsInstance(future, job.JobEventWatchFuture)
        event = future.get_event()
        self.assertIsInstance(event, job.EventLogEvent)
        self.assertEqual(event.name, "submit")
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
            self.assertTrue(type(dict(event)), dict)
            self.assertIs(type(event.timestamp), float)
            self.assertIs(type(event.name), str)
            self.assertIs(type(event.context), dict)
            events.append(event.name)
        self.assertEqual(len(events), 10)

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

    def test_20_005_1_job_event_watch_with_cancel_stop_true(self):
        jobid = job.submit(
            self.fh, JobspecV1.from_command(["sleep", "3"]), waitable=True
        )
        self.assertTrue(jobid > 0)
        events = []
        future = job.event_watch_async(self.fh, jobid)

        def cb(future, events):
            event = future.get_event()
            if event.name == "start":
                future.cancel(stop=True)
            events.append(event.name)

        future.then(cb, events)
        self.fh.reactor_run()

        # Last event should be "start"
        self.assertEqual(events[-1], "start")
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

    def test_20_007_job_event_wait_exception(self):
        event = None
        jobid = job.submit(
            self.fh, JobspecV1.from_command(["sleep", "0"], num_tasks=128)
        )
        self.assertTrue(jobid > 0)
        try:
            event = job.event_wait(self.fh, jobid, "start")
        except job.JobException as err:
            self.assertEqual(err.severity, 0)
            self.assertEqual(err.type, "alloc")
            self.assertGreater(err.timestamp, 0.0)
        self.assertIs(event, None)
        try:
            event = job.event_wait(self.fh, jobid, "start", raiseJobException=False)
        except OSError as err:
            self.assertEqual(err.errno, errno.ENODATA)
        self.assertIs(event, None)

    def test_21_stdio_new_methods(self):
        """Test official getter/setter methods for stdio properties
        Ensure for now that output sets the alias "stdout", error sets "stderr"
        and input sets "stdin".
        """
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        streams = {"error": "stderr", "output": "stdout", "input": "stdin"}
        for name in ("error", "output", "input"):
            stream = streams[name]
            self.assertEqual(getattr(jobspec, name), None)
            for path in ("foo.txt", "bar.md", "foo.json"):
                setattr(jobspec, name, path)
                self.assertEqual(getattr(jobspec, name), path)
                self.assertEqual(getattr(jobspec, stream), path)
            with self.assertRaises(TypeError):
                setattr(jobspec, name, None)

    def test_21_stdio(self):
        """Test getter/setter methods for stdio properties"""
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        for stream in ("stderr", "stdout", "stdin"):
            self.assertEqual(getattr(jobspec, stream), None)
            for path in ("foo.txt", "bar.md", "foo.json"):
                setattr(jobspec, stream, path)
                self.assertEqual(getattr(jobspec, stream), path)
            with self.assertRaises(TypeError):
                setattr(jobspec, stream, None)

        with self.assertRaises(TypeError):
            jobspec.unbuffered = 1

        jobspec.unbuffered = True
        self.assertTrue(jobspec.unbuffered)
        self.assertEqual(
            jobspec.getattr("shell.options.output.stderr.buffer.type"), "none"
        )
        self.assertEqual(
            jobspec.getattr("shell.options.output.stdout.buffer.type"), "none"
        )
        self.assertEqual(jobspec.getattr("shell.options.output.batch-timeout"), 0.05)

        jobspec.unbuffered = False
        self.assertFalse(jobspec.unbuffered)

        # jobspec.unbuffered = True keeps modified batch-timeout
        jobspec.unbuffered = True
        jobspec.setattr_shell_option("output.batch-timeout", 1.0)
        jobspec.unbuffered = False
        self.assertFalse(jobspec.unbuffered)
        self.assertEqual(jobspec.getattr("shell.options.output.batch-timeout"), 1.0)

    def test_22_from_batch_command(self):
        """Test that `from_nest_command` produces a valid jobspec"""
        jobid = job.submit(
            self.fh, JobspecV1.from_batch_command("#!/bin/sh\nsleep 0", "nested sleep")
        )
        self.assertGreater(jobid, 0)
        # test that a shebang is required
        with self.assertRaises(ValueError):
            job.submit(
                self.fh,
                JobspecV1.from_batch_command("sleep 0", "nested sleep with no shebang"),
            )
        # extra parameters are accepted:
        environment = {"TEST": 1, "FOO": "BAR"}
        env_expand = {"EXPAND": "{{tmpdir}}"}
        rlimits = {"nofile": 12000}

        jobspec = JobspecV1.from_batch_command(
            "#!/bin/sh\0sleep0",
            "testjob",
            environment=environment,
            env_expand=env_expand,
            rlimits=rlimits,
            duration=60.0,
            cwd="/my/cwd",
            output="output-file",
            error="error-file",
            label_io=True,
            unbuffered=True,
            queue="testq",
            bank="testbank",
        )
        self.assertDictEqual(jobspec.environment, environment)
        self.assertDictEqual(
            jobspec.getattr("system.shell.options.env-expand"), env_expand
        )
        self.assertDictEqual(jobspec.getattr("system.shell.options.rlimit"), rlimits)
        self.assertEqual(jobspec.duration, 60)
        self.assertEqual(jobspec.cwd, "/my/cwd")
        self.assertEqual(jobspec.output, "output-file")
        self.assertEqual(jobspec.error, "error-file")
        self.assertTrue(jobspec.getattr("system.shell.options.output.stdout.label"))
        self.assertTrue(jobspec.getattr("system.shell.options.output.stderr.label"))
        self.assertEqual(
            jobspec.getattr("system.shell.options.output.stdout.buffer.type"),
            "none",
        )
        self.assertEqual(jobspec.getattr("system.queue"), "testq")
        self.assertEqual(jobspec.getattr("system.bank"), "testbank")

    def test_23_from_nest_command(self):
        """Test that `from_batch_command` produces a valid jobspec"""
        jobid = job.submit(self.fh, JobspecV1.from_nest_command(["sleep", "0"]))
        self.assertGreater(jobid, 0)

        # check that extra parameters are accepted:
        jobspec = JobspecV1.from_nest_command(
            ["sleep", "0"],
            name="test",
            duration="10m",
            cwd="/my/cwd",
            output="output-file",
            error="error-file",
            label_io=True,
            unbuffered=True,
            queue="testq",
            bank="testbank",
        )
        self.assertEqual(jobspec.duration, 600)
        self.assertEqual(jobspec.cwd, "/my/cwd")
        self.assertEqual(jobspec.output, "output-file")
        self.assertEqual(jobspec.error, "error-file")
        self.assertTrue(
            jobspec.getattr("attributes.system.shell.options.output.stdout.label")
        )
        self.assertTrue(
            jobspec.getattr("attributes.system.shell.options.output.stderr.label")
        )
        self.assertEqual(
            jobspec.getattr(
                "attributes.system.shell.options.output.stdout.buffer.type"
            ),
            "none",
        )
        self.assertEqual(jobspec.getattr("system.queue"), "testq")
        self.assertEqual(jobspec.getattr("system.bank"), "testbank")
        self.assertEqual(jobspec.getattr("system.job.name"), "test")

    def test_24_jobid(self):
        """Test JobID class"""
        parse_tests = [
            {
                "int": 0,
                "dec": "0",
                "hex": "0x0",
                "dothex": "0000.0000.0000.0000",
                "kvs": "job.0000.0000.0000.0000",
                "f58": "ƒ1",
                "words": "academy-academy-academy--academy-academy-academy",
                "emoji": "😃",
            },
            {
                "int": 1,
                "dec": "1",
                "hex": "0x1",
                "dothex": "0000.0000.0000.0001",
                "kvs": "job.0000.0000.0000.0001",
                "f58": "ƒ2",
                "words": "acrobat-academy-academy--academy-academy-academy",
                "emoji": "😄",
            },
            {
                "int": 65535,
                "dec": "65535",
                "hex": "0xffff",
                "dothex": "0000.0000.0000.ffff",
                "kvs": "job.0000.0000.0000.ffff",
                "f58": "ƒLUv",
                "words": "nevada-archive-academy--academy-academy-academy",
                "emoji": "💁📚",
            },
            {
                "int": 6787342413402046,
                "dec": "6787342413402046",
                "hex": "0x181d0d4d850fbe",
                "dothex": "0018.1d0d.4d85.0fbe",
                "kvs": "job.0018.1d0d.4d85.0fbe",
                "f58": "ƒuzzybunny",
                "words": "cake-plume-nepal--neuron-pencil-academy",
                "emoji": "👴😱🔚🎮🕙🚩",
            },
            {
                "int": 18446744073709551614,
                "dec": "18446744073709551614",
                "hex": "0xfffffffffffffffe",
                "dothex": "ffff.ffff.ffff.fffe",
                "kvs": "job.ffff.ffff.ffff.fffe",
                "f58": "ƒjpXCZedGfVP",
                "words": "mustang-analyze-verbal--natural-analyze-verbal",
                "emoji": "🚹💗💧👗😷📷📙",
            },
        ]
        for test in parse_tests:
            for key in test:
                if key == "f58" and self.use_ascii:
                    continue
                jobid_int = test["int"]
                jobid = job.JobID(test[key])
                self.assertEqual(jobid, jobid_int)
                jobid_repr = repr(jobid)
                self.assertEqual(jobid_repr, f"JobID({jobid_int})")
                if key not in ["int", "dec"]:
                    # Ensure encode back to same type works
                    self.assertEqual(getattr(jobid, key), test[key])

        # JobID can also take a JobID
        jobid1 = job.JobID(1234)
        jobid2 = job.JobID(jobid1)
        self.assertEqual(jobid1, jobid2)
        self.assertEqual(jobid2.orig, "1234")

    def test_25_job_list_attrs(self):
        expected_attrs = [
            "userid",
            "urgency",
            "priority",
            "t_submit",
            "t_depend",
            "t_run",
            "t_cleanup",
            "t_inactive",
            "state",
            "name",
            "cwd",
            "queue",
            "project",
            "bank",
            "ntasks",
            "ncores",
            "duration",
            "nnodes",
            "ranks",
            "nodelist",
            "success",
            "exception_occurred",
            "exception_type",
            "exception_severity",
            "exception_note",
            "result",
            "expiration",
            "annotations",
            "waitstatus",
            "dependencies",
            "all",
        ]
        valid_attrs = self.fh.rpc("job-list.list-attrs", "{}").get()["attrs"]
        self.assertEqual(set(valid_attrs), set(expected_attrs))

    def test_30_job_stats_sync(self):
        stats = JobStats(self.fh)

        # stats are uninitialized at first:
        self.assertEqual(stats.active, -1)
        self.assertEqual(stats.inactive, -1)

        # synchronous update
        stats.update_sync()
        self.assertGreater(stats.inactive, 0)

    def test_31_job_stats_async(self):
        called = [False]

        def cb(stats, mykw=None):
            called[0] = True
            self.assertGreater(stats.inactive, 0)
            self.assertEqual(mykw, "mykw")

        stats = JobStats(self.fh)

        # stats are uninitialized at first:
        self.assertEqual(stats.active, -1)
        self.assertEqual(stats.inactive, -1)

        # asynchronous update, no callback
        stats.update()
        self.assertEqual(stats.active, -1)
        self.assertEqual(stats.inactive, -1)

        self.fh.reactor_run()
        self.assertGreater(stats.inactive, 0)
        self.assertFalse(called[0])

        # asynchronous update, with callback
        stats.update(callback=cb, mykw="mykw")
        self.fh.reactor_run()
        self.assertTrue(called[0])

    def assertJobInfoEqual(self, x, y, msg=None):

        self.assertEqual(x.id, y.id)
        self.assertEqual(x.result, y.result)
        self.assertEqual(x.returncode, y.returncode)
        self.assertEqual(x.waitstatus, y.waitstatus)
        if y.t_run == 0.0:
            self.assertEqual(x.runtime, y.runtime)
        else:
            self.assertGreater(x.runtime, 0.0)

        self.assertEqual(x.exception.occurred, y.exception.occurred)

        if y.exception.occurred:
            self.assertEqual(x.exception.type, y.exception.type)
            self.assertEqual(x.exception.severity, y.exception.severity)
            if y.exception.note:
                self.assertRegex(x.exception.note, y.exception.note)
            else:
                self.assertEqual(x.exception.note, y.exception.note)

    def test_32_job_result(self):
        result = {}
        ids = []

        def cb(future, jobid):
            result[jobid] = future

        ids.append(job.submit(self.fh, JobspecV1.from_command(["true"])))
        ids.append(job.submit(self.fh, JobspecV1.from_command(["false"])))
        ids.append(job.submit(self.fh, JobspecV1.from_command(["nosuchprog"])))
        ids.append(job.submit(self.fh, JobspecV1.from_command(["sleep", "120"])))

        # Submit held job so we can cancel before RUN state
        ids.append(job.submit(self.fh, JobspecV1.from_command(["true"]), urgency=0))
        job.cancel(self.fh, ids[4])

        # Submit another held job so we can raise non-cancel exception
        # before RUN:
        ids.append(job.submit(self.fh, JobspecV1.from_command(["true"]), urgency=0))
        self.fh.rpc(
            "job-manager.raise", {"id": ids[5], "severity": 0, "type": "test"}
        ).get()

        for jobid in ids:
            flux.job.result_async(self.fh, jobid).then(cb, jobid)

        def cancel_on_start(future, jobid):
            event = future.get_event()
            if event is None:
                return
            if event.name == "shell.start":
                job.cancel(self.fh, jobid)
                future.cancel()

        job.event_watch_async(self.fh, ids[3], eventlog="guest.exec.eventlog").then(
            cancel_on_start, ids[3]
        )

        self.fh.reactor_run()
        self.assertEqual(len(result.keys()), len(ids))

        self.addTypeEqualityFunc(JobInfo, self.assertJobInfoEqual)

        self.assertEqual(
            result[ids[0]].get_info(),
            JobInfo(
                {
                    "id": ids[0],
                    "result": flux.constants.FLUX_JOB_RESULT_COMPLETED,
                    "t_start": 1.0,
                    "t_run": 2.0,
                    "t_cleanup": 3.0,
                    "waitstatus": 0,
                    "exception_occurred": False,
                }
            ),
        )

        # t_remaining returns 0 from JobInfo returned from result():
        self.assertEqual(result[ids[0]].get_info().t_remaining, 0.0)

        # Ensure to_dict() works for a JobInfo returned from result():
        job_dict = result[ids[0]].get_info().to_dict()
        self.assertIsInstance(job_dict, dict)
        self.assertEqual(job_dict["id"], ids[0])
        self.assertEqual(job_dict["result"], "COMPLETED")
        self.assertEqual(job_dict["returncode"], 0)
        self.assertEqual(job_dict["duration"], 0.0)

        self.assertEqual(
            result[ids[1]].get_info(),
            JobInfo(
                {
                    "id": ids[1],
                    "result": flux.constants.FLUX_JOB_RESULT_FAILED,
                    "t_submit": 1.0,
                    "t_run": 2.0,
                    "t_cleanup": 3.0,
                    "waitstatus": 256,
                    "exception_occurred": False,
                }
            ),
        )
        # Ensure to_dict() works for a JobInfo returned from result():
        job_dict = result[ids[1]].get_info().to_dict()
        self.assertIsInstance(job_dict, dict)
        self.assertEqual(job_dict["id"], ids[1])
        self.assertEqual(job_dict["result"], "FAILED")
        self.assertEqual(job_dict["returncode"], 1)
        self.assertEqual(job_dict["duration"], 0.0)

        self.assertEqual(
            result[ids[2]].get_info(),
            JobInfo(
                {
                    "id": ids[2],
                    "result": flux.constants.FLUX_JOB_RESULT_FAILED,
                    "t_submit": 1.0,
                    "t_run": 2.0,
                    "t_cleanup": 3.0,
                    "waitstatus": 32512,
                    "exception_occurred": True,
                    "exception_type": "exec",
                    "exception_note": "task 0.*: start failed: nosuchprog: "
                    "No such file or directory",
                    "exception_severity": 0,
                }
            ),
        )
        self.assertEqual(
            result[ids[3]].get_info(),
            JobInfo(
                {
                    "id": ids[3],
                    "result": flux.constants.FLUX_JOB_RESULT_CANCELED,
                    "t_submit": 1.0,
                    "t_run": 2.0,
                    "t_cleanup": 3.0,
                    "waitstatus": 36608,  # 143<<8
                    "exception_occurred": True,
                    "exception_type": "cancel",
                    "exception_note": "",
                    "exception_severity": 0,
                }
            ),
        )
        self.assertEqual(
            result[ids[4]].get_info(),
            JobInfo(
                {
                    "id": ids[4],
                    "result": flux.constants.FLUX_JOB_RESULT_CANCELED,
                    "t_submit": 0.0,
                    "exception_occurred": True,
                    "exception_type": "cancel",
                    "exception_note": "",
                    "exception_severity": 0,
                }
            ),
        )
        self.assertEqual(
            result[ids[5]].get_info(),
            JobInfo(
                {
                    "id": ids[5],
                    "result": flux.constants.FLUX_JOB_RESULT_FAILED,
                    "t_submit": 0.0,
                    "exception_occurred": True,
                    "exception_type": "test",
                    "exception_note": "",
                    "exception_severity": 0,
                }
            ),
        )
        # Explicitly check returncode for job that failed before start
        # without a cancel exception. This is done since returncode is
        # a derived attribute and we want to ensure it is explicitly 1:
        self.assertEqual(result[ids[5]].get_info().returncode, 1)
        # synchronous job.result() test
        self.assertEqual(job.result(self.fh, ids[3]), result[ids[3]].get_info())

    def test_33_get_job(self):
        self.sleep_jobspec = JobspecV1.from_command(["sleep", "5"])
        jobid = job.submit(self.fh, self.sleep_jobspec)
        info = job.get_job(self.fh, jobid)
        self.assertIsInstance(info, dict)
        for key in [
            "id",
            "userid",
            "urgency",
            "priority",
            "t_submit",
            "t_depend",
            "state",
            "name",
            "cwd",
            "ntasks",
            "ncores",
            "duration",
            "nnodes",
            "result",
            "runtime",
            "returncode",
            "waitstatus",
            "nodelist",
            "exception",
        ]:
            self.assertIn(key, info)

        self.assertEqual(info["id"], jobid)
        self.assertEqual(info["name"], "sleep")
        self.assertTrue(info["state"] in ["SCHED", "DEPEND", "RUN"])
        self.assertEqual(info["ntasks"], 1)
        self.assertEqual(info["ncores"], 1)

        # Test a job that does not exist
        info = job.get_job(self.fh, 123456)
        self.assertIsNone(info)

    def test_34_timeleft(self):
        spec = JobspecV1.from_command(
            ["python3", "-c", "import flux; print(flux.job.timeleft())"],
            duration="1m",
        )
        jobid = job.submit(self.fh, spec, waitable=True)
        job.wait(self.fh, jobid=jobid)
        try:
            job.timeleft()
            job.timeleft(self.fh)
        except OSError:
            pass

    def test_35_setattr_defaults(self):
        """Test setattr setting defaults"""
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        jobspec.setattr("cow", 1)
        jobspec.setattr("system.cat", 2)
        jobspec.setattr("user.dog", 3)
        jobspec.setattr("attributes.system.chicken", 4)
        jobspec.setattr("attributes.user.duck", 5)
        jobspec.setattr("attributes.goat", 6)
        self.assertEqual(jobspec.getattr("cow"), 1)
        self.assertEqual(jobspec.getattr("system.cow"), 1)
        self.assertEqual(jobspec.getattr("attributes.system.cow"), 1)

        self.assertEqual(jobspec.getattr("cat"), 2)
        self.assertEqual(jobspec.getattr("system.cat"), 2)
        self.assertEqual(jobspec.getattr("attributes.system.cat"), 2)

        self.assertEqual(jobspec.getattr("user.dog"), 3)
        self.assertEqual(jobspec.getattr("attributes.user.dog"), 3)

        self.assertEqual(jobspec.getattr("chicken"), 4)
        self.assertEqual(jobspec.getattr("system.chicken"), 4)
        self.assertEqual(jobspec.getattr("attributes.system.chicken"), 4)

        self.assertEqual(jobspec.getattr("user.duck"), 5)
        self.assertEqual(jobspec.getattr("attributes.user.duck"), 5)

        self.assertEqual(jobspec.getattr("attributes.goat"), 6)

    def test_36_str(self):
        """Test string representation of a basic jobspec"""
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        jobspec.setattr("cow", 1)
        jobspec.setattr("system.cat", 2)
        jobspec.setattr("user.dog", 3)
        jobspec.setattr("attributes.system.chicken", 4)
        jobspec.setattr("attributes.user.duck", 5)
        jobspec.setattr("attributes.goat", 6)
        self.assertEqual(
            str(jobspec),
            "{'resources': [{'type': 'slot', 'count': 1, 'label': 'task', 'with': [{'type': 'core', 'count': 1}]}], 'tasks': [{'command': ['app'], 'slot': 'foo', 'count': {'per_slot': 1}}], 'attributes': {'system': {'duration': 0, 'cow': 1, 'cat': 2, 'chicken': 4}, 'user': {'dog': 3, 'duck': 5}, 'goat': 6}, 'version': 1}",
        )

    def test_37_repr(self):
        """Test __repr__ method of Jobspec"""
        jobspec = Jobspec.from_yaml_stream(self.basic_jobspec)
        self.assertEqual(eval(repr(jobspec)).jobspec, jobspec.jobspec)
        jobspec.cwd = "/foo/bar"
        jobspec.stdout = "/bar/baz"
        jobspec.duration = 1000.3133
        self.assertEqual(eval(repr(jobspec)).jobspec, jobspec.jobspec)

    def test_37_test_bad_extra_args(self):
        """Test extra Jobspec constructor args with bad values"""
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], duration="1f")
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], environment="foo")
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], env_expand=1)
        with self.assertRaises(ValueError):
            JobspecV1.from_command(["sleep", "0"], rlimits=True)

    def test_38_environment_default(self):
        jobspec = JobspecV1.from_command(["sleep", "0"])
        self.assertEqual(jobspec.environment, dict(os.environ))

        jobspec = JobspecV1.from_command(["sleep", "0"], environment={})
        self.assertEqual(jobspec.environment, {})


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
