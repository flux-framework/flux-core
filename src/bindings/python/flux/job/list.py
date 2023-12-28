###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import errno
import os
import pwd

import flux.constants
from flux.future import WaitAllFuture
from flux.job import JobID
from flux.job.info import JobInfo
from flux.rpc import RPC


class JobListRPC(RPC):
    def get_jobs(self):
        """Returns all jobs in the RPC."""
        return self.get()["jobs"]

    def get_jobinfos(self):
        """Yields a JobInfo object for each job in its current state.

        :rtype: JobInfo
        """
        for job in self.get_jobs():
            yield JobInfo(job)


# Due to subtleties in the python bindings and this call, this binding
# is more of a reimplementation of flux_job_list() instead of calling
# the flux_job_list() C function directly.  Some reasons:
#
# - Desire to return a Python RPC class and use its get() method
# - Desired return value is json array, not a single value
#
# pylint: disable=dangerous-default-value
def job_list(
    flux_handle,
    max_entries=1000,
    attrs=["all"],
    userid=os.getuid(),
    states=0,
    results=0,
    since=0.0,
    name=None,
    queue=None,
):
    # N.B. an "and" operation with no values returns everything
    constraint = {"and": []}
    if userid != flux.constants.FLUX_USERID_UNKNOWN:
        constraint["and"].append({"userid": [userid]})
    if name:
        constraint["and"].append({"name": [name]})
    if queue:
        constraint["and"].append({"queue": [queue]})
    if states and results:
        tmp = {"or": []}
        tmp["or"].append({"states": [states]})
        tmp["or"].append({"results": [results]})
        constraint["and"].append(tmp)
    elif states:
        constraint["and"].append({"states": [states]})
    elif results:
        constraint["and"].append({"results": [results]})
    payload = {
        "max_entries": int(max_entries),
        "attrs": attrs,
        "since": since,
        "constraint": constraint,
    }
    return JobListRPC(flux_handle, "job-list.list", payload)


def job_list_inactive(
    flux_handle, since=0.0, max_entries=1000, attrs=["all"], name=None, queue=None
):
    """Same as ``flux.job.list.job_list``, but lists only inactive jobs."""
    return job_list(
        flux_handle,
        max_entries=max_entries,
        attrs=attrs,
        userid=flux.constants.FLUX_USERID_UNKNOWN,
        states=flux.constants.FLUX_JOB_STATE_INACTIVE,
        since=since,
        name=name,
        queue=queue,
    )


class JobListIdRPC(RPC):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.jobid = None

    def get_job(self):
        return self.get()["job"]

    def get_jobinfo(self):
        """Returns a ``JobInfo`` object for the job.

        :rtype: JobInfo
        """
        return JobInfo(self.get_job())


# list-id is not like list or list-inactive, it doesn't return an
# array, so don't use JobListRPC
def job_list_id(flux_handle, jobid, attrs=["all"]):
    """Query job information for a single ``jobid``.

    Sends an RPC to the job-list module to query information about the provided jobid.
    Use the ``get_job()`` or ``get_jobinfo()`` method on the returned ``JobListIdRPC`` to
    obtain the job data as a dict or a JobInfo object.

    :rtype: JobListIdRPC
    """
    payload = {"id": int(jobid), "attrs": attrs}
    rpc = JobListIdRPC(flux_handle, "job-list.list-id", payload)
    #  save original JobId argument for error reporting
    rpc.jobid = jobid
    return rpc


# get_job is the single variant of job_list_id, and returns the
# expected data structure (dict) to describe one job (or None)
def get_job(flux_handle, jobid):
    """Get job information dictionary based on a jobid

    This is a courtesy, blocking function for users looking for
    details about a job after submission. The dictionary includes
    the job identifier, userid that submit it, urgency, priority,
    t_submit, t_depend, (and others when finished), state, name,
    ntasks, ncores, duration, nnodes, result, runtime, returncode,
    waitstatus, nodelist, and exception type, severity, and note.
    """
    payload = {"id": int(jobid), "attrs": ["all"]}
    rpc = JobListIdRPC(flux_handle, "job-list.list-id", payload)
    try:
        jobinfo = rpc.get_jobinfo()

    # The job does not exist!
    except FileNotFoundError:
        return None

    return jobinfo.to_dict(filtered=False)


class JobListIdsFuture(WaitAllFuture):
    """Simulate interface of JobListRPC for listing multiple jobids"""

    def __init__(self):
        super(JobListIdsFuture, self).__init__()
        self.errors = []

    def get_jobs(self):
        """get all successful results, appending errors into self.errors"""
        jobs = []
        #  Wait for all jobid RPCs to complete
        self.wait_for()

        #  Get all successful jobs, accumulate errors in self.errors
        for child in self.children:
            try:
                jobs.append(child.get_job())
            except EnvironmentError as err:
                if err.errno == errno.ENOENT:
                    msg = f"JobID {child.jobid.orig} unknown"
                else:
                    msg = f"rpc: {err.strerror}"
                self.errors.append(msg)
        return jobs

    def get_jobinfos(self):
        """get all successful results as list of JobInfo objects

        Any errors are appended to self.errors.
        """
        return [JobInfo(job) for job in self.get_jobs()]


class JobList:
    """User friendly class for querying lists of jobs from Flux

    By default a JobList will query the last ``max_entries`` jobs for all
    users. Other filter parameters can be passed to the constructor or
    the ``set_user()`` and ``add_filter()`` methods.

    :flux_handle: A Flux handle obtained from flux.Flux()
    :attrs: Optional list of job attributes to fetch. (default is all attrs)
    :filters: List of strings defining the results or states to filter. E.g.,
              [ "pending", "running" ].
    :ids: List of jobids to return. Other filters are ignored if ``ids`` is
          not empty.
    :user: Username or userid for which to fetch jobs. Default is all users.
    :max_entries: Maximum number of jobs to return
    :since: Limit jobs to those that have been active since a given timestamp.
    :name: Limit jobs to those with a specific name.
    :queue: Limit jobs to those submitted to a specific queue.
    """

    # pylint: disable=too-many-instance-attributes

    STATES = {
        "depend": flux.constants.FLUX_JOB_STATE_DEPEND,
        "priority": flux.constants.FLUX_JOB_STATE_PRIORITY,
        "sched": flux.constants.FLUX_JOB_STATE_SCHED,
        "run": flux.constants.FLUX_JOB_STATE_RUN,
        "cleanup": flux.constants.FLUX_JOB_STATE_CLEANUP,
        "inactive": flux.constants.FLUX_JOB_STATE_INACTIVE,
        "pending": flux.constants.FLUX_JOB_STATE_PENDING,
        "running": flux.constants.FLUX_JOB_STATE_RUNNING,
        "active": flux.constants.FLUX_JOB_STATE_ACTIVE,
    }
    RESULTS = {
        "completed": flux.constants.FLUX_JOB_RESULT_COMPLETED,
        "failed": flux.constants.FLUX_JOB_RESULT_FAILED,
        "canceled": flux.constants.FLUX_JOB_RESULT_CANCELED,
        "timeout": flux.constants.FLUX_JOB_RESULT_TIMEOUT,
    }

    def __init__(
        self,
        flux_handle,
        attrs=["all"],
        filters=[],
        ids=[],
        user=None,
        max_entries=1000,
        since=0.0,
        name=None,
        queue=None,
    ):
        self.handle = flux_handle
        self.attrs = list(attrs)
        self.states = 0
        self.results = 0
        self.max_entries = max_entries
        self.since = since
        self.name = name
        self.queue = queue
        self.ids = list(map(JobID, ids)) if ids else None
        self.errors = []
        for fname in filters:
            for x in fname.split(","):
                self.add_filter(x)
        self.set_user(user)

    def set_user(self, user):
        """Only return jobs for user (may be a username or userid)"""
        if user is None:
            self.userid = os.getuid()
        elif user == "all":
            self.userid = flux.constants.FLUX_USERID_UNKNOWN
        else:
            try:
                self.userid = pwd.getpwnam(user).pw_uid
            except KeyError:
                try:
                    self.userid = int(user)
                except ValueError:
                    raise ValueError(f"Invalid user {user} specified")

    def add_filter(self, fname):
        """Append a state or result filter to JobList query"""
        fname = fname.lower()
        if fname in self.STATES:
            self.states |= self.STATES[fname]
        elif fname in self.RESULTS:
            self.results |= self.RESULTS[fname]
        else:
            raise ValueError(f"Invalid filter specified: {fname}")

    def fetch_jobs(self):
        """Initiate the JobList query to the Flux job-info module

        JobList.fetch_jobs() returns a JobListRPC or JobListIdsFuture,
        either of which will be fulfilled when the job data is available.

        Once the Future has been fulfilled, a list of JobInfo objects
        can be obtained via JobList.jobs(). If JobList.errors is non-empty,
        then it will contain a list of errors returned via the query.
        """
        if self.ids:
            listids = JobListIdsFuture()
            for jobid in self.ids:
                listids.push(job_list_id(self.handle, jobid, self.attrs))
            return listids
        return job_list(
            self.handle,
            max_entries=self.max_entries,
            attrs=self.attrs,
            userid=self.userid,
            states=self.states,
            results=self.results,
            since=self.since,
            name=self.name,
            queue=self.queue,
        )

    def jobs(self):
        """Synchronously fetch a list of JobInfo objects from JobList query

        If the Future object returned by JobList.fetch_jobs has not yet been
        fulfilled (e.g. is_ready() returns False), then this call may block.
        Otherwise, returns a list of JobInfo objects for all jobs returned
        from the underlying job listing RPC.
        """
        rpc = self.fetch_jobs()
        jobs = rpc.get_jobs()
        if hasattr(rpc, "errors"):
            self.errors = rpc.errors
        return [JobInfo(job) for job in jobs]
