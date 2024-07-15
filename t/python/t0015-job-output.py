#!/usr/bin/python3
###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import concurrent.futures
import errno
import os
import unittest

import flux
import subflux  # noqa: F401 - for PYTHONPATH
from flux.constants import FLUX_JOB_URGENCY_DEFAULT, FLUX_JOB_URGENCY_HOLD
from flux.job import (
    JobspecV1,
    event_wait,
    job_output,
    output_event_watch,
    output_event_watch_async,
    output_watch,
    output_watch_async,
    output_watch_lines,
    output_watch_lines_async,
)
from flux.job.output import LOG_QUIET, LOG_TRACE


def __flux_size():
    return 2


class TestJobOutput(unittest.TestCase):

    test_stdout = "line 1\nline 2\nline 3\n"
    test_stderr = "error 1\nerror 2\n"

    def submit(
        self,
        output=None,
        error=None,
        hold=False,
        redirect=None,
        verbose=False,
        ntasks=1,
        cmd=None,
    ):
        if output is None:
            output = self.test_stdout
        if error is None:
            error = self.test_stderr
        if cmd is None:
            cmd = f"printf '{output}'; printf '{error}' >&2"
        command = ["bash", "-c", cmd]
        jobspec = JobspecV1.from_command(
            command=command, num_tasks=ntasks, num_nodes=1, cores_per_task=1
        )
        urgency = FLUX_JOB_URGENCY_DEFAULT
        jobspec.setattr_shell_option("cpu-affinity", "off")
        jobspec.environment = dict(os.environ)
        if hold:
            urgency = FLUX_JOB_URGENCY_HOLD
        if verbose:
            jobspec.setattr_shell_option("verbose", 1)
        if redirect is not None:
            jobspec.stdout = redirect
        return flux.job.submit(self.fh, jobspec, waitable=True, urgency=urgency)

    def release_job(self, jobid):
        # Release job by setting urgency to default:
        self.fh.rpc(
            "job-manager.urgency",
            {"id": int(jobid), "urgency": FLUX_JOB_URGENCY_DEFAULT},
        ).get()

    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()
        self.executor = concurrent.futures.ThreadPoolExecutor(1)

    def test_output_invalid_args(self):
        with self.assertRaises(FileNotFoundError):
            job_output(self.fh, 123)
        with self.assertRaises(FileNotFoundError):
            job_output(self.fh, 123, nowait=True)
        with self.assertRaises(FileNotFoundError):
            for event in output_event_watch(self.fh, 123):
                print(event)

    def test_job_output(self):
        jobid = self.submit()
        output = job_output(self.fh, jobid)
        self.assertEqual(output.stdout, self.test_stdout)
        self.assertEqual(output.stderr, self.test_stderr)
        self.assertEqual(output.log, "")

    def test_job_output_labelio(self):
        jobid = self.submit(ntasks=2)
        output = job_output(self.fh, jobid, labelio=True)
        self.assertIn("1: line 1", output.stdout)
        self.assertIn("1: error 1", output.stderr)

    def test_job_output_labelio_task_filter(self):
        jobid = self.submit(ntasks=2)
        output = job_output(self.fh, jobid, tasks="1", labelio=True)
        self.assertIn("1: line 1", output.stdout)
        self.assertIn("1: error 1", output.stderr)
        self.assertNotIn("0: line 1", output.stdout)
        self.assertNotIn("0: error 1", output.stderr)

    def test_job_output_with_logs(self):
        jobid = self.submit(verbose=True)
        output = job_output(self.fh, jobid)
        self.assertEqual(output.stdout, self.test_stdout)
        # By default log messages are folded in with stderr:
        self.assertIn("error 1", output.stderr)
        self.assertIn("flux-shell[0]", output.stderr)
        self.assertEqual(output.log, "")

        # Now, set log_stderr_level=-1.
        # output.log should contain all log messages
        output = job_output(self.fh, jobid, log_stderr_level=-1)
        self.assertEqual(output.stdout, self.test_stdout)
        self.assertEqual(output.stderr, self.test_stderr)
        self.assertTrue(len(output.log) > 0)

    def test_job_output_nowait(self):
        jobid = self.submit(cmd="echo first; sleep 100; echo second")
        # wait for output to be ready:
        event_wait(self.fh, jobid, "shell.init", "guest.exec.eventlog")
        output = job_output(self.fh, jobid, nowait=True)
        self.assertNotIn("second", output.stdout)
        flux.job.cancel(self.fh, jobid)

    def test_pending_job_output(self):
        jobid = self.submit(hold=True)

        def get_output():
            # This function will be run in another thread, so use a new
            # Flux handle:
            return job_output(flux.Flux(), jobid)

        # get_output() should block until the job is released, so run it
        # in a separate thread using the ThreadPoolExecutor
        future = self.executor.submit(get_output)

        self.release_job(jobid)
        try:
            output = future.result(timeout=15)
        except TimeoutError:
            os.system(f"flux job eventlog {jobid}")
            try:
                flux.job.cancel(self.fh, jobid)
            except FileNotFoundError:
                pass
            raise TimeoutError from None
        self.assertEqual(output.stdout, self.test_stdout)
        self.assertEqual(output.stderr, self.test_stderr)
        self.assertEqual(output.log, "")

    def test_pending_job_cancel(self):
        jobid = self.submit(hold=True)

        def get_output():
            # This function will be run in another thread, so use a new
            # Flux handle:
            return job_output(flux.Flux(), jobid)

        # get_output() should block until the job is released, so run it
        # in a separate thread using the ThreadPoolExecutor
        future = self.executor.submit(get_output)

        # Cancel pending job
        flux.job.cancel(self.fh, jobid)

        # Job output should raise JobException
        with self.assertRaises(flux.job.JobException):
            future.result(timeout=15)

    def test_pending_job_nowait(self):
        jobid = self.submit(hold=True)
        with self.assertRaises(FileNotFoundError):
            job_output(self.fh, jobid, nowait=True)
        flux.job.cancel(self.fh, jobid)

    def test_output_event_watch(self):
        jobid = self.submit()
        stdout = ""
        stderr = ""
        for event in output_event_watch(self.fh, jobid):
            if event.name == "data" and event.data:
                if event.stream == "stdout":
                    stdout += event.data
                else:
                    stderr += event.data
        self.assertEqual(stdout, self.test_stdout)
        self.assertEqual(stderr, self.test_stderr)

    def test_output_event_watch_labelio(self):
        jobid = self.submit(ntasks=2)
        got_task = [False, False]
        for event in output_event_watch(self.fh, jobid, labelio=True):
            if event.name == "data" and event.data and "line 3" in event.data:
                for rank in event.rank.ids:
                    got_task[rank] = True
        self.assertTrue(got_task[0])
        self.assertTrue(got_task[1])

    def test_output_event_watch_nowait(self):
        def event_watch(jobid):
            stdout = ""
            stderr = ""
            #  This function may be run in a thread, create new flux handle
            fh = flux.Flux()
            for event in output_event_watch(fh, jobid, nowait=True):
                if event.name == "data" and event.data:
                    if event.stream == "stdout":
                        stdout += event.data
                    else:
                        stderr += event.data
            return stdout, stderr

        jobid = self.submit(hold=True)

        #  Note: We don't test for FileNotFoundError here because there
        #  ultimately no way to make a race free test. There would have to
        #  be some way to ensure the watch request has been registered first.
        self.release_job(jobid)
        event_wait(self.fh, jobid, "shell.init", "guest.exec.eventlog")

        # Now nowait should work:
        stdout, stderr = event_watch(jobid)
        self.assertEqual(stdout, self.test_stdout)
        self.assertEqual(stderr, self.test_stderr)

    def test_output_event_watch_async(self):
        jobid = self.submit(verbose=True)

        result = {"stdout": "", "stderr": "", "log": ""}

        def output_event_watch_cb(future, result):
            event = future.get_event()
            if event is None:
                return
            if event.name == "data" and event.data is not None:
                result[event.stream] += event.data
            elif event.name == "log":
                result["log"] += event.message + "\n"

        future = output_event_watch_async(self.fh, jobid)
        self.assertIsInstance(future, flux.job.output.JobOutputEventWatch)
        future.then(output_event_watch_cb, result, timeout=60)
        self.fh.reactor_run()

        self.assertEqual(result["stdout"], self.test_stdout)
        self.assertEqual(result["stderr"], self.test_stderr)
        self.assertTrue(len(result["log"]) > 0)

    def test_output_event_watch_async_cancel(self):
        jobid = self.submit(cmd="echo before; sleep 30; echo after")

        result = {"stdout": "", "stderr": "", "log": ""}

        def output_event_watch_cb(future, result):
            event = future.get_event()
            if event is None:
                return
            if event.name == "data" and event.data is not None:
                result[event.stream] += event.data
                if "before" in event.data:
                    flux.job.cancel(self.fh, jobid)
            elif event.name == "log":
                result["log"] += event.message + "\n"

        future = output_event_watch_async(self.fh, jobid)
        future.then(output_event_watch_cb, result, timeout=15)
        self.fh.reactor_run()
        self.assertIn("before", result["stdout"])

    def test_output_watch(self):
        def do_watch(
            jobid,
            result=None,
            label=False,
            log_stderr_level=LOG_TRACE,
            nowait=False,
        ):
            if result is None:
                result = {}
            for stream, data in output_watch(
                self.fh,
                jobid,
                labelio=label,
                log_stderr_level=log_stderr_level,
                nowait=nowait,
            ):
                result.setdefault(stream, []).append(data)
            return {
                key: "".join(result.get(key, [])) for key in ("stdout", "stderr", "log")
            }

        result = do_watch(self.submit())
        self.assertEqual(result["stdout"], self.test_stdout)
        self.assertEqual(result["stderr"], self.test_stderr)
        self.assertEqual(result["log"], "")

        # Separate logs
        result = do_watch(self.submit(verbose=2), log_stderr_level=-1)
        self.assertEqual(result["stdout"], self.test_stdout)
        self.assertEqual(result["stderr"], self.test_stderr)
        self.assertTrue(len(result["log"]) > 0)

        # Redirect
        result = do_watch(self.submit(redirect="{{tmpdir}}/test.out"))
        self.assertRegex(result["stderr"], "stdout redirected to .*/test.out")
        self.assertRegex(result["stderr"], "stderr redirected to .*/test.out")

        # Labelio
        result = do_watch(self.submit(ntasks=2), label=True)
        self.assertIn("0: line 1", result["stdout"])
        self.assertIn("1: line 1", result["stdout"])
        self.assertIn("0: error 1", result["stderr"])
        self.assertIn("1: error 1", result["stderr"])

        # nowait=True works
        jobid = self.submit()
        event_wait(self.fh, jobid, "shell.init", "guest.exec.eventlog")

        result = do_watch(jobid, nowait=True)
        self.assertIn("line 1", result["stdout"])
        self.assertIn("error 1", result["stderr"])

        # canceled job
        jobid = self.submit(hold=True)
        flux.job.cancel(self.fh, jobid)

        # catch OSError raised from non-started job:
        with self.assertRaises(OSError) as context:
            result = do_watch(jobid)
            # Also, should have received a job.exception message on stderr:
            self.assertIn("job.exception", "\n".join(result[0]["stderr"]))
        exception = context.exception
        self.assertEqual(exception.strerror, f"job {jobid} never started")
        self.assertEqual(exception.errno, errno.EIO)

    def test_output_watch_async(self):
        def watch_cb(future, result):
            try:
                stream, data = future.get_output()
            except Exception as exc:
                stream = "stderr"
                data = str(exc)
                future.reset()
            result.setdefault(stream, []).append(data)

        def get_results(result):
            return {
                key: "".join(result.get(key, [])) for key in ("stdout", "stderr", "log")
            }

        results = {}

        # Basic
        results["basic"] = {}
        output_watch_async(self.fh, self.submit()).then(watch_cb, results["basic"])

        # Separate logs
        results["log"] = {}
        output_watch_async(
            self.fh, self.submit(verbose=2), log_stderr_level=LOG_QUIET
        ).then(watch_cb, results["log"])

        # Labelio
        results["labelio"] = {}
        output_watch_async(self.fh, self.submit(ntasks=2), labelio=True).then(
            watch_cb, results["labelio"]
        )

        # Cancel
        jobid = self.submit(hold=True)
        flux.job.cancel(self.fh, jobid)
        results["cancel"] = {}
        output_watch_async(self.fh, jobid).then(watch_cb, results["cancel"])

        self.fh.reactor_run()

        for test in ("basic", "log", "labelio", "cancel"):
            results[test] = get_results(results[test])

        self.assertEqual(results["basic"]["stdout"], self.test_stdout)
        self.assertEqual(results["basic"]["stderr"], self.test_stderr)
        self.assertEqual(results["basic"]["log"], "")

        self.assertEqual(results["log"]["stdout"], self.test_stdout)
        self.assertEqual(results["log"]["stderr"], self.test_stderr)
        self.assertTrue(len(results["log"]["log"]) > 0)

        self.assertIn("0: line 1", results["labelio"]["stdout"])
        self.assertIn("1: line 1", results["labelio"]["stdout"])
        self.assertIn("0: error 1", results["labelio"]["stderr"])
        self.assertIn("1: error 1", results["labelio"]["stderr"])

        # Also, should have received a job.exception message on stderr:
        self.assertIn("job.exception", results["cancel"]["stderr"])
        self.assertIn("never started", results["cancel"]["stderr"])

    def test_output_watch_async_exception(self):
        def watch_cb_with_error(future):
            # Test raising error from typo:
            stream, line = notfuture.getline()  # noqa: F821

        jobid = self.submit()
        output_watch_async(self.fh, jobid).then(watch_cb_with_error)
        with self.assertRaises(NameError):
            self.fh.reactor_run()

    def test_output_watch_lines(self):
        def do_watch_lines(
            jobid,
            result=None,
            label=False,
            log_stderr_level=LOG_TRACE,
            keepends=False,
            nojoin=False,
            nowait=False,
        ):
            if result is None:
                result = {}
            count = 0
            for stream, line in output_watch_lines(
                self.fh,
                jobid,
                labelio=label,
                log_stderr_level=log_stderr_level,
                keepends=keepends,
                nowait=nowait,
            ):
                count += 1
                result.setdefault(stream, []).append(line)
            if nojoin:
                return result, count
            return {
                key: "\n".join(result.get(key, [])) + "\n"
                for key in ("stdout", "stderr", "log")
            }, count

        exp_count = self.test_stdout.count("\n") + self.test_stderr.count("\n")

        result, count = do_watch_lines(self.submit())
        self.assertEqual(count, exp_count)
        self.assertEqual(result["stdout"], self.test_stdout)
        self.assertEqual(result["stderr"], self.test_stderr)
        self.assertEqual(result["log"], "\n")

        # Separate logs
        result, count = do_watch_lines(self.submit(verbose=2), log_stderr_level=-1)
        # Don't check `count`, unknown number of "log" lines
        self.assertEqual(result["stdout"], self.test_stdout)
        self.assertEqual(result["stderr"], self.test_stderr)
        self.assertTrue(len(result["log"]) > 0)

        # Redirect
        result, count = do_watch_lines(self.submit(redirect="{{tmpdir}}/test.out"))
        self.assertEqual(count, 2)
        self.assertRegex(result["stderr"], "stdout redirected to .*/test.out")
        self.assertRegex(result["stderr"], "stderr redirected to .*/test.out")

        # Labelio
        result, count = do_watch_lines(self.submit(ntasks=2), label=True)
        self.assertEqual(count, 2 * exp_count)
        self.assertIn("0: line 1", result["stdout"])
        self.assertIn("1: line 1", result["stdout"])
        self.assertIn("0: error 1", result["stderr"])
        self.assertIn("1: error 1", result["stderr"])

        # keepends=True
        result, count = do_watch_lines(self.submit(), keepends=True, nojoin=True)
        self.assertEqual(count, exp_count)
        self.assertIn("line 1\n", result["stdout"])
        self.assertIn("error 1\n", result["stderr"])

        # nowait=True works
        jobid = self.submit()
        event_wait(self.fh, jobid, "shell.init", "guest.exec.eventlog")

        result, count = do_watch_lines(jobid, nowait=True)
        self.assertEqual(count, exp_count)
        self.assertIn("line 1", result["stdout"])
        self.assertIn("error 1", result["stderr"])

        # canceled job
        jobid = self.submit(hold=True)
        flux.job.cancel(self.fh, jobid)

        # catch OSError raised from non-started job:
        with self.assertRaises(OSError) as context:
            result, count = do_watch_lines(jobid)
            # Also, should have received a job.exception message on stderr:
            self.assertIn("job.exception", "\n".join(result[0]["stderr"]))
        exception = context.exception
        self.assertEqual(exception.strerror, f"job {jobid} never started")
        self.assertEqual(exception.errno, errno.EIO)

    def test_output_watch_lines_async(self):
        def watch_cb(future, result):
            try:
                stream, line = future.getline()
            except Exception as exc:
                stream = "stderr"
                line = str(exc)
                future.reset()
            result.setdefault(stream, []).append(line)
            if "count" in result:
                if stream is not None:
                    result["count"] += 1
            else:
                result["count"] = 1

        def get_results(result):
            results = {
                key: "\n".join(result.get(key, [])) + "\n"
                for key in ("stdout", "stderr", "log")
            }
            results["count"] = result["count"]
            return results

        results = {}
        exp_count = self.test_stdout.count("\n") + self.test_stderr.count("\n")

        # Basic
        results["basic"] = {}
        output_watch_lines_async(self.fh, self.submit()).then(
            watch_cb, results["basic"]
        )
        # Separate logs
        results["log"] = {}
        output_watch_lines_async(
            self.fh, self.submit(verbose=2), log_stderr_level=LOG_QUIET
        ).then(watch_cb, results["log"])
        # Labelio
        results["labelio"] = {}
        output_watch_lines_async(self.fh, self.submit(ntasks=2), labelio=True).then(
            watch_cb, results["labelio"]
        )

        # Cancel
        jobid = self.submit(hold=True)
        flux.job.cancel(self.fh, jobid)
        results["cancel"] = {}
        output_watch_lines_async(self.fh, jobid).then(watch_cb, results["cancel"])

        self.fh.reactor_run()

        for test in ("basic", "log", "labelio", "cancel"):
            results[test] = get_results(results[test])

        # check results:
        self.assertEqual(results["basic"]["count"], exp_count)
        self.assertEqual(results["basic"]["stdout"], self.test_stdout)
        self.assertEqual(results["basic"]["stderr"], self.test_stderr)
        self.assertEqual(results["basic"]["log"], "\n")

        #  Unknown number of "log" entries so don't test count
        self.assertEqual(results["log"]["stdout"], self.test_stdout)
        self.assertEqual(results["log"]["stderr"], self.test_stderr)
        self.assertTrue(len(results["log"]["log"]) > 0)

        self.assertEqual(results["labelio"]["count"], 2 * exp_count)
        self.assertIn("0: line 1", results["labelio"]["stdout"])
        self.assertIn("1: line 1", results["labelio"]["stdout"])
        self.assertIn("0: error 1", results["labelio"]["stderr"])
        self.assertIn("1: error 1", results["labelio"]["stderr"])

        # check canceled job for a job.exception message on stderr:
        self.assertIn("job.exception", results["cancel"]["stderr"])
        self.assertIn("never started", results["cancel"]["stderr"])

    def test_output_watch_lines_async_exception(self):
        def watch_cb_with_error(future):
            # Test raising error from typo:
            stream, line = notfuture.getline()  # noqa: F821

        jobid = self.submit()
        output_watch_lines_async(self.fh, jobid).then(watch_cb_with_error)
        with self.assertRaises(NameError):
            self.fh.reactor_run()


if __name__ == "__main__":
    from subflux import rerun_under_flux

    if rerun_under_flux(size=__flux_size(), personality="job"):
        from pycotap import LogMode, TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(LogMode.LogToYAML, LogMode.LogToError))
