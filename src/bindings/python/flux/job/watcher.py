#############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import os
import sys
import time
from collections import Counter

import flux
from flux.job import output_watch_async
from flux.progress import ProgressBar


class JobStatus:
    """
    Simple convenience class for caching job "state" in the JobProgressBar
    and JobWatch classes.

    Attributes:
        id: The job id
        status: This job's current simplified status (See below for possible
            status values)
        exitcode: The exit code of the job (0 for success, otherwise failure)

    Current valid statuses include:
    - pending
    - running
    - complete
    - failed
    """

    def __init__(self, job):
        """
        Initialize a JobStatus object using an instance of flux.job.JobInfo
        """
        if not isinstance(job, flux.job.JobInfo):
            raise ValueError("JobStatus requires an object of type JobInfo")
        self.id = job.id
        self.status, self.exitcode = self._jobinfo_get_status_and_exitcode(job)
        self._events = Counter()

    def _jobinfo_get_status_and_exitcode(self, job):
        #  Return simplified job status and exitcode if job is inactive
        status = job.status
        if status in ("DEPEND", "PRIORITY", "SCHED"):
            return "pending", 0
        elif status in ("RUN", "CLEANUP"):
            return "running", 0
        elif status == "COMPLETED":
            return "complete", 0
        elif status in ("FAILED", "CANCELED", "TIMEOUT"):
            return "failed", job.returncode
        raise ValueError(f"unknown job status {status}")

    @property
    def active(self):
        """True if the job is still active"""
        return self.status in ("running", "pending")

    def add_event(self, name):
        """
        Add an event to the event counter
        """
        self._events[name] += 1

    def event_count(self, name):
        """
        Return the number of times event ``name`` has been seen for this job
        """
        return self._events[name]

    def has_event(self, name):
        """
        Return True if this job has seen event ``name``
        """
        return self.event_count(name) > 0


class JobProgressBar(ProgressBar):
    """Progress bar for multiple jobs

    The JobProgressBar class is a ProgressBar specific to monitoring
    progress of a group of jobs. It displays a progress bar with the
    number of pending, running, complete and failed jobs on the left,
    and a percent complete and elapsed timer by default on the right,
    with a progress bar in the middle.

    Once a JobProgressBar object is initialized, jobs to track should
    be added with JobProgressBar.add_job or JobProgressBar.add_jobs.
    Subsequently, eventlog events for jobs are fed into the progress
    bar via JobProgressBar.process_event. To advance the progress bar,
    the ``update()`` method should be called.

    Attributes:
        jobs: list of JobStatus objects being monitored
        starttime: start time used for the elapsed timer and jobs/s display.
            By default this will be the minimum submission time of all jobs
            being monitored. To choose a different starttime, manually set
            the starttime in the initializer.
        total: total number of jobs being monitored
        pending: current pending job count
        running: current running job count
        complete: current complete job count (successful jobs)
        failed: current failed job count (failed, canceled, timeout)
    """

    def __init__(
        self,
        flux_handle,
        starttime=None,
        jps=False,
        counter_width=3,
        update_interval=0.25,
    ):
        """
        Initialize an instance of a JobProgressBar.

        Args
            starttime (float): if set, use this value as the start time for
                calculation of the progress bar elapsed time of jobs/s.
            jps (bool): show job/s on right hand side instead of elapsed time
            counter_width (int): width reserved for pending, running, etc.
                counters on left hand display (default=3)
            update_interval (float): interval in floating point seconds at
                which the progress bar is forced to be updated. For an elapsed
                timer, this should be at least 1.0 (default=0.25)
        """
        before = (
            "PD:{pending:<{width}} R:{running:<{width}} "
            "CD:{complete:<{width}} F:{failed:<{width}} "
        )
        after = "{percent:5.1f}% {elapsed.dt}"
        if jps:
            after = "{percent:5.1f}% {jps:4.1f} job/s"
        self.jobs = {}
        self._finished_jobs = {}
        self._started = False
        self.starttime = starttime
        self.status = Counter()

        timer = flux_handle.timer_watcher_create(
            0, lambda *x: self.redraw(), repeat=update_interval
        )

        super().__init__(
            timer=timer,
            total=0,
            width=counter_width,
            before=before,
            after=after,
            pending=0,
            running=0,
            complete=0,
            failed=0,
            jps=0,
        )

    def start(self):
        """
        Start JobProgressBar operation.

        Initialize and start timer watchers, display initial progress bar,
        and if not set, initialize the elapsed start time.
        """
        if self._started:
            return

        self._started = True
        self.timer.start()
        #  Don't let this timer watcher contribute to the reactor's
        #   "active" reference count:
        #
        self.timer.flux_handle.reactor_decref()
        super().start()
        #  Override superclass `_t0` attribute to elapsed time is computed
        #  from this value and not the time of super().start():
        #
        if self.starttime is not None:
            self._t0 = self.starttime

    def update(self):
        """
        Update job state counts for ProgressBar and refresh display
        """
        super().update(
            advance=0,
            pending=self.pending,
            running=self.running,
            complete=self.complete,
            failed=self.failed,
        )

    def advance(self, **kwargs):
        """
        Advance progress bar (e.g. if one job has completed).
        Args:
            kwargs: keyword args passed to ProgressBar.update
        """
        super().update(advance=1, **kwargs)

    def add_job(self, job):
        """
        Begin monitoring the progress of a job.

        Args:
            job (JobInfo): The job to begin monitoring
        """
        if not isinstance(job, flux.job.JobInfo):
            raise ValueError("add_job takes an argument of type JobInfo")
        if job.id in self.jobs:
            raise ValueError(f"job {job.id} is already being monitored")
        if self._t0 is None or job.t_submit < self._t0:
            self._t0 = job.t_submit
        jobstatus = JobStatus(job)
        self.jobs[job.id] = jobstatus
        self.total += 1
        #  Increment current status attribute:
        setattr(self, jobstatus.status, getattr(self, jobstatus.status) + 1)
        #  Update counts/redraw
        self.update()

    def add_jobs(self, *jobs):
        """
        Add multiple jobs to a JobProgressBar instance
        """
        for job in jobs:
            self.add_job(job)

    def jobs_per_sec(self):
        """
        Return the current job throughput.
        """
        return (self.count + 1) / (time.time() - self._t0)

    def _set_running(self, jobid):
        job = self.jobs[jobid]
        if job.status != "pending":
            raise ValueError(f"set_running: {job.id} not pending")
        job.status = "running"
        self.pending -= 1
        self.running += 1

    def _set_complete(self, jobid):
        job = self.jobs[jobid]
        if job.status != "running":
            raise ValueError(f"set_complete: {job.id} not running")
        job.status = "complete"
        self.running -= 1
        self.complete += 1

    def _set_failed(self, jobid):
        job = self.jobs[jobid]
        if job.status == "pending":
            self.pending -= 1
        elif job.status == "running":
            self.running -= 1
        self.failed += 1
        job.status = "failed"

    def process_event(self, jobid, event=None):
        """
        Process an event for a job, updating job's progress if necessary.

        Args:
            jobid: job id
            event: event entry to process. If None, then the job is considered
                complete, i.e. no more events will be received for this job.
        """
        job = self.jobs[jobid]
        if event is None:
            #
            #  Caller should set event=None when no more events are expected
            #  for this job. This is where we advance the progress bar instead
            #  of at the 'finish' or 'exception' events since this allows the
            #  caller to determine when progress should advance (e.g. if the
            #  use case is to only wait for job 'start' events.
            #
            if jobid not in self._finished_jobs:
                self._finished_jobs[jobid] = True
                self.advance(jps=self.jobs_per_sec())
        elif event.name == "alloc" and job.status == "pending":
            self._set_running(jobid)
        elif event.name == "exception" and event.context["severity"] == 0:
            #
            #  Exceptions only need to be specially handled in the
            #   pending state. If the job is running and gets an exception
            #   then a finish event will be posted.
            #
            if job.status == "pending":
                self._set_failed(jobid)
        elif event.name == "finish" and job.active:
            job.exitcode = event.context["status"]
            if job.exitcode == 0:
                self._set_complete(jobid)
            else:
                self._set_failed(jobid)
        self.update()


class JobWatcher:
    """Watch output and status for multiple jobs.

    The JobWatcher class can watch the status, output, and logs for one or
    more jobs, optionally including a progress bar for use in a tty. This
    is the class that implements the ``--watch`` option of ``flux submit``
    and ``flux bulksubmit``.

    """

    class JobWatchStatus(JobStatus):
        """
        JobStatus class with extra attributes for use in JobWatcher
        """

        def __init__(self, job, stdout, stderr, wait="clean"):
            super().__init__(job)
            self.stdout = stdout
            self.stderr = stderr
            self.wait = wait

    def __init__(
        self,
        flux_handle,
        jobs=None,
        progress=False,
        jps=False,
        wait="clean",
        watch=True,
        log_events=False,
        log_status=False,
        stdout=sys.stdout,
        stderr=sys.stderr,
        labelio=False,
        starttime=None,
    ):
        """
        Initialize an instance of the JobWatcher class.

        Args:
            flux_handle (Flux): Flux handle
            jobs (list of JobInfo): initialize JobWatcher with a list of jobs
            progress (bool): Show status and throughput progress bar
                (default=False)
            jps (bool): with ``progress=True`` show jobs per second instead of
                timer on right hand side of progress bar
            wait (str): Event to wait for before terminating watch of job.
                (default="clean")
            log_events (bool): Log all events on stderr (default=False)
            log_status (bool): Log final status of jobs if applicable
                (default=False)
            stdout (TextIOWrapper): Default stdout location (default=sys.stdout)
            stderr (TextIOWrapper): Default stderr location (default=sys.stderr)
            labelio (bool): Label lines of output with jobid and taskid
            starttime (float): If not None, start elapsed timer at this time.
        """
        self.flux_handle = flux_handle
        self.progress = None
        self.wait = wait
        self.watch = watch
        self.t0 = starttime
        self.log_events = log_events
        self.log_status = log_status
        self.stdout = self._reopen(stdout)
        self.stderr = self._reopen(stderr)
        self.labelio = labelio
        self.exitcode = 0
        self.show_progress = progress
        self.progress = JobProgressBar(flux_handle, starttime=self.t0, jps=jps)

        self._states = Counter()

        if jobs:
            self.add_jobs(*jobs)

    @staticmethod
    def _reopen(stream):
        """reconfigure/reopen stream with correct encoding and error handling"""
        try:
            # reconfigure() only available in >=3.7
            stream.reconfigure(encoding="utf-8", errors="surrogateescape")
            return stream
        except AttributeError:
            return open(
                stream.fileno(),
                mode="w",
                encoding="utf-8",
                errors="surrogateescape",
                closefd=False,
            )

    @staticmethod
    def _status_to_exitcode(status):
        """Calculate exitcode from job status"""
        if os.WIFEXITED(status):
            status = os.WEXITSTATUS(status)
        elif os.WIFSIGNALED(status):
            status = 128 + os.WTERMSIG(status)
        return status

    def start(self):
        """
        Start JobWatcher progress bar if configured
        """
        if self.show_progress:
            self.progress.start()
        return self

    def stop(self):
        """
        Stop JobWatcher progress bar if configured
        """
        if self.show_progress:
            self.progress.stop()
        return self

    def add_jobs(self, *jobs, stdout=None, stderr=None, wait="clean"):
        """
        Begin monitoring the progress of a set of jobs

        Args:
            *jobs (JobID): one or more jobs to monitor
            stdout (TextIOWrapper): destination for stdout for this job
                or jobs. If None, use default for this JobWatcher instance.
            stderr: (TextIOWrapper): destination for stderr for this job
                or jobs (if None, then set to same location as ``stdout``)
            wait: event at which to stop watching the job (default=clean)
        """

        self.progress.add_jobs(*jobs)

        if stdout is None:
            stdout = self._reopen(self.stdout)
        if stderr is None:
            stderr = self._reopen(self.stderr)

        for job in jobs:
            if not self.t0 or job.t_submit < self.t0:
                self.t0 = job.t_submit

            job_status = self.JobWatchStatus(job, stdout, stderr, wait=wait)
            flux.job.event_watch_async(self.flux_handle, job.id).then(
                self._event_watch_cb, job_status
            )
            if not job_status.active:
                self._progress_update(job_status)
        return self

    def add_jobid(self, jobid, stdout=None, stderr=None, wait="clean"):
        """
        Begin monitoring the progress of a job by jobid

        Args:
            jobid (JobID): one or more jobs to monitor
            stdout (TextIOWrapper): destination for stdout for this job
                or jobs. If None, use default for this JobWatcher instance.
            stderr: (TextIOWrapper): destination for stderr for this job
                or jobs (if None, then set to same location as ``stdout``)
            wait: event at which to stop watching the job (default=clean)
        """
        #
        #  add_jobs() expects a JobInfo object, but caller only has a jobid.
        #  Create a mock JobInfo object assuming the job is in SCHED state
        #  so that this job is properly initialized for watching.
        #
        job = flux.job.JobInfo(
            {
                "id": jobid,
                "state": flux.constants.FLUX_JOB_STATE_SCHED,
                "t_submit": time.time(),
            }
        )
        self.add_jobs(job, stdout=stdout, stderr=stderr, wait=wait)

    def _progress_update(self, job, event=None):
        self.progress.process_event(job.id, event)

    def _log(self, job, timestamp, msg):
        dt = timestamp - self.t0
        print(f"{job.id.f58}: {dt:4.3f}s: {msg}", file=job.stderr)

    def _log_event(self, job, event, event_prefix=""):
        if self.log_events and event is not None:
            self._log(
                job,
                event.timestamp,
                f"{event_prefix}{event.name} {event.context_string}",
            )

    def _event_watch_cb(self, future, job):
        event = future.get_event()

        # Update progress meter if being used
        self._progress_update(job, event)

        # End of eventlog
        if event is None:
            return

        job.add_event(event.name)

        if event.timestamp < self.t0:
            self.t0 = event.timestamp

        self._log_event(job, event)

        if event.name == "exception":
            severity = event.context["severity"]
            if severity == 0:
                #  If job didn't start then it failed to execute.
                #  Set status to failed and emit exception error on stderr:
                if not job.has_event("start"):
                    self.exitcode = max(self.exitcode, 1)
                    job.status = "failed"

                #  If the output eventlog is not being watched because the
                #  shell never initialized, then print the exception error
                #  to stderr:
                if not job.has_event("shell.init"):
                    print(
                        flux.job.output.JobExceptionEvent(event).render(),
                        file=job.stderr,
                    )
        elif event.name == "alloc":
            job.status = "running"
        elif event.name == "start":
            if self.watch or job.wait.startswith("exec."):
                flux.job.event_watch_async(
                    self.flux_handle, job.id, eventlog="guest.exec.eventlog"
                ).then(self._exec_event_cb, job, future)
        elif event.name == "finish":
            #
            # job finished. Collect wait status into self.exitcode
            #
            status = event.context["status"]
            self.exitcode = max(self.exitcode, self._status_to_exitcode(status))
            job.status = "complete"
            if status > 0:
                job.status = "failed"
            if self.log_status:
                self._log(job, event.timestamp, f"{job.status}: status={status}")
        if job.wait and job.wait == event.name:
            #
            # Done with this job: update progress bar and cancel future
            #
            self._progress_update(job)
            future.cancel(stop=True)

    def _exec_event_cb(self, future, job, main_eventlog_future):
        event = future.get_event()
        if event is None:
            return
        self._log_event(job, event, event_prefix="exec.")
        if self.watch and event.name == "shell.init":
            #  shell.init event indicates output eventlog is ready
            #  (use nowait=True to avoid watching intermediate eventlogs)
            #
            output_watch_async(
                self.flux_handle,
                job.id,
                labelio=self.labelio,
                nowait=True,
            ).then(self._output_watch_cb, job)

            if not job.wait or not job.wait.startswith("exec."):
                # No more events from exec eventlog are needed
                future.cancel(stop=True)

        if job.wait and job.wait == f"exec.{event.name}":
            #  Done with this job
            #
            future.cancel(stop=True)
            main_eventlog_future.cancel(stop=True)

    def _output_watch_cb(self, future, job):
        stream, data = future.get_output()
        if stream is not None:
            output_stream = getattr(job, stream)
            if self.labelio:
                for line in data.splitlines(keepends=True):
                    output_stream.write(f"{job.id}: {line}")
            else:
                output_stream.write(data)
        else:
            for stream in ("stdout", "stderr"):
                getattr(job, stream).flush()
