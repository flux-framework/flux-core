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

import os
import time
import unittest

import flux
import flux.constants
import subflux  # noqa: F401 - for PYTHONPATH
from flux.job import JobInfo, JobspecV1
from flux.job.watcher import JobStatus, JobWatcher


def __flux_size():
    return 1


def make_jobinfo(state, result=None, waitstatus=None, jobid=1234):
    """Create a minimal JobInfo dict for unit testing."""
    info = {"id": jobid, "state": state, "t_submit": time.time()}
    if result is not None:
        info["result"] = result
    if waitstatus is not None:
        info["waitstatus"] = waitstatus
    return JobInfo(info)


class TestJobStatus(unittest.TestCase):
    """Unit tests for JobStatus - no Flux broker required."""

    def test_invalid_arg(self):
        with self.assertRaises(ValueError):
            JobStatus("not-a-jobinfo")

    def test_pending_states(self):
        for state in (
            flux.constants.FLUX_JOB_STATE_DEPEND,
            flux.constants.FLUX_JOB_STATE_PRIORITY,
            flux.constants.FLUX_JOB_STATE_SCHED,
        ):
            job = make_jobinfo(state)
            js = JobStatus(job)
            self.assertEqual(js.status, "pending")
            self.assertEqual(js.exitcode, 0)
            self.assertTrue(js.active)

    def test_running_states(self):
        for state in (
            flux.constants.FLUX_JOB_STATE_RUN,
            flux.constants.FLUX_JOB_STATE_CLEANUP,
        ):
            job = make_jobinfo(state)
            js = JobStatus(job)
            self.assertEqual(js.status, "running")
            self.assertEqual(js.exitcode, 0)
            self.assertTrue(js.active)

    def test_complete(self):
        job = make_jobinfo(
            flux.constants.FLUX_JOB_STATE_INACTIVE,
            result=flux.constants.FLUX_JOB_RESULT_COMPLETED,
        )
        js = JobStatus(job)
        self.assertEqual(js.status, "complete")
        self.assertEqual(js.exitcode, 0)
        self.assertFalse(js.active)

    def test_failed(self):
        # exit code 2: waitpid returns 2<<8
        waitstatus = 2 << 8
        job = make_jobinfo(
            flux.constants.FLUX_JOB_STATE_INACTIVE,
            result=flux.constants.FLUX_JOB_RESULT_FAILED,
            waitstatus=waitstatus,
        )
        js = JobStatus(job)
        self.assertEqual(js.status, "failed")
        self.assertEqual(js.exitcode, 2)
        self.assertFalse(js.active)

    def test_canceled(self):
        job = make_jobinfo(
            flux.constants.FLUX_JOB_STATE_INACTIVE,
            result=flux.constants.FLUX_JOB_RESULT_CANCELED,
        )
        js = JobStatus(job)
        self.assertEqual(js.status, "failed")
        self.assertFalse(js.active)

    def test_timeout(self):
        job = make_jobinfo(
            flux.constants.FLUX_JOB_STATE_INACTIVE,
            result=flux.constants.FLUX_JOB_RESULT_TIMEOUT,
            waitstatus=0,
        )
        js = JobStatus(job)
        self.assertEqual(js.status, "failed")
        self.assertFalse(js.active)

    def test_event_tracking(self):
        job = make_jobinfo(flux.constants.FLUX_JOB_STATE_SCHED)
        js = JobStatus(job)
        self.assertFalse(js.has_event("start"))
        self.assertEqual(js.event_count("start"), 0)
        js.add_event("start")
        self.assertTrue(js.has_event("start"))
        self.assertEqual(js.event_count("start"), 1)
        js.add_event("start")
        self.assertEqual(js.event_count("start"), 2)

    def test_jobid_stored(self):
        job = make_jobinfo(flux.constants.FLUX_JOB_STATE_SCHED, jobid=5678)
        js = JobStatus(job)
        self.assertEqual(int(js.id), 5678)


class TestJobWatcherOutputCallback(unittest.TestCase):
    """Integration tests for JobWatcher.output_callback."""

    test_stdout = "hello from stdout\n"
    test_stderr = "hello from stderr\n"

    @classmethod
    def setUpClass(cls):
        cls.fh = flux.Flux()

    def submit(self, cmd=None):
        if cmd is None:
            cmd = f"printf '{self.test_stdout}'; " f"printf '{self.test_stderr}' >&2"
        jobspec = JobspecV1.from_command(
            ["bash", "-c", cmd], num_tasks=1, num_nodes=1, cores_per_task=1
        )
        jobspec.setattr_shell_option("cpu-affinity", "off")
        jobspec.environment = dict(os.environ)
        return flux.job.submit(self.fh, jobspec, waitable=True)

    def watch_job(self, jobid, **kwargs):
        """Run a JobWatcher for a single jobid, returning the collected output."""
        collected = {}

        def callback(jid, stream, data):
            if stream is not None:
                collected.setdefault(stream, []).append((jid, data))

        watcher = JobWatcher(self.fh, output_callback=callback, **kwargs)
        watcher.start()
        watcher.add_jobid(jobid)
        self.fh.reactor_run()
        return collected

    def test_output_callback_called(self):
        """output_callback receives stdout and stderr from a job."""
        jobid = self.submit()
        collected = self.watch_job(jobid)
        stdout = "".join(d for _, d in collected.get("stdout", []))
        stderr = "".join(d for _, d in collected.get("stderr", []))
        self.assertEqual(stdout, self.test_stdout)
        self.assertEqual(stderr, self.test_stderr)

    def test_output_callback_receives_jobid(self):
        """output_callback receives the correct jobid."""
        jobid = self.submit()
        collected = self.watch_job(jobid)
        for stream, items in collected.items():
            for jid, _ in items:
                self.assertEqual(int(jid), jobid)

    def test_output_callback_multiple_jobs(self):
        """output_callback identifies each job correctly when watching multiple."""
        jobid1 = self.submit(cmd="echo job1")
        jobid2 = self.submit(cmd="echo job2")

        collected = {}

        def callback(jid, stream, data):
            if stream is not None:
                collected.setdefault(int(jid), []).append(data)

        watcher = JobWatcher(self.fh, output_callback=callback)
        watcher.start()
        watcher.add_jobid(jobid1)
        watcher.add_jobid(jobid2)
        self.fh.reactor_run()

        self.assertIn("job1\n", "".join(collected.get(jobid1, [])))
        self.assertIn("job2\n", "".join(collected.get(jobid2, [])))

    def test_output_callback_no_stdout_stderr_written(self):
        """When output_callback is set, nothing is written to stdout/stderr."""
        import io

        captured_out = io.StringIO()
        captured_err = io.StringIO()
        callback_data = []

        def callback(jid, stream, data):
            if stream is not None:
                callback_data.append(data)

        jobid = self.submit()

        # Pass dummy streams; they should not be written when callback is set
        watcher = JobWatcher(
            self.fh,
            output_callback=callback,
            stdout=captured_out,
            stderr=captured_err,
        )
        watcher.start()
        watcher.add_jobid(jobid)
        self.fh.reactor_run()

        # Callback should have received output
        self.assertTrue(len(callback_data) > 0)
        # Dummy streams should be untouched
        self.assertEqual(captured_out.getvalue(), "")
        self.assertEqual(captured_err.getvalue(), "")


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import LogMode, TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(LogMode.LogToYAML, LogMode.LogToError))
