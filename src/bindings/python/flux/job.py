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
import math
import json
import errno
import datetime
import collections
import collections.abc as abc
import numbers
import signal

import six
import yaml

import flux.kvs
from flux import constants
from flux.wrapper import Wrapper
from flux.util import check_future_error, parse_fsd
from flux.future import Future
from flux.rpc import RPC
from _flux._core import ffi, lib


class JobWrapper(Wrapper):
    def __init__(self):
        super(JobWrapper, self).__init__(ffi, lib, prefixes=["flux_job_"])


RAW = JobWrapper()


def _convert_jobspec_arg_to_string(jobspec):
    """
    Convert a jobspec argument into a string.  A valid jobspec argument is:
    - An instance of the Jobspec class
    - A string (i.e., bytes, str, or unicode)

    :raises EnvironmentError: jobspec is None or NULL
    :raises TypeError: jobspec is neither a Jobspec nor a string
    """
    if isinstance(jobspec, Jobspec):
        jobspec = jobspec.dumps()
    elif isinstance(jobspec, six.text_type):
        jobspec = jobspec.encode("utf-8", errors="surrogateescape")
    elif jobspec is None or jobspec == ffi.NULL:
        # catch this here rather than in C for a better error message
        raise EnvironmentError(errno.EINVAL, "jobspec must not be None/NULL")
    elif not isinstance(jobspec, six.binary_type):
        raise TypeError(
            "jobpsec must be a Jobspec or string (either binary or unicode)"
        )
    return jobspec


def job_kvs(flux_handle, jobid):
    """
    :returns: The KVS directory of the given job
    :rtype: KVSDir
    """

    path_len = 1024
    buf = ffi.new("char[]", path_len)
    RAW.kvs_key(buf, path_len, jobid, "")
    kvs_key = ffi.string(buf, path_len)
    return flux.kvs.get_dir(flux_handle, kvs_key)


def job_kvs_guest(flux_handle, jobid):
    """
    :returns: The KVS guest directory of the given job
    :rtype: KVSDir
    """

    path_len = 1024
    buf = ffi.new("char[]", path_len)
    RAW.kvs_guest_key(buf, path_len, jobid, "")
    kvs_key = ffi.string(buf, path_len)
    return flux.kvs.get_dir(flux_handle, kvs_key)


def id_parse(jobid_str):
    """
    returns: An integer jobid
    :rtype int
    """
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.id_parse(jobid_str, jobid)
    return int(jobid[0])


def id_encode(jobid, encoding="f58"):
    """
    returns: Jobid encoded in encoding
    :rtype str
    """
    buflen = 128
    buf = ffi.new("char[]", buflen)
    RAW.id_encode(int(jobid), encoding, buf, buflen)
    return ffi.string(buf, buflen).decode("utf-8")


class JobID(int):
    """Class used to represent a Flux JOBID

    JobID is a subclass of `int`, so may be used in place of integer.
    However, a JobID may be created from any valid RFC 19 FLUID
    encoding, including:

     - decimal integer (no prefix)
     - hexidecimal integer (prefix 0x)
     - dotted hex (dothex) (xxxx.xxxx.xxxx.xxxx)
     - kvs dir (dotted hex with `job.` prefix)
     - RFC19 F58: (Base58 encoding with prefix `ƒ` or `f`)

    A JobID object also has properties for encoding a JOBID into each
    of the above representations, e.g. jobid.f85, jobid.words, jobid.dothex...

    """

    def __new__(cls, value, *args, **kwargs):
        if isinstance(value, int):
            jobid = value
        else:
            jobid = id_parse(value)
        return super(cls, cls).__new__(cls, jobid)

    def encode(self, encoding="dec"):
        """Encode a JobID to alternate supported format"""
        return id_encode(self, encoding)

    @property
    def dec(self):
        """Return decimal integer representation of a JobID"""
        return self.encode()

    @property
    def f58(self):
        """Return RFC19 F58 representation of a JobID"""
        return self.encode("f58")

    @property
    def hex(self):
        """Return 0x-prefixed hexidecimal representation of a JobID"""
        return self.encode("hex")

    @property
    def dothex(self):
        """Return dotted hexidecimal representation of a JobID"""
        return self.encode("dothex")

    @property
    def words(self):
        """Return words (mnemonic) representation of a JobID"""
        return self.encode("words")

    @property
    def kvs(self):
        """Return KVS directory path of a JobID"""
        return self.encode("kvs")

    def __str__(self):
        return self.encode("f58")

    def __repr__(self):
        return f"JobID({self.dec})"


class SubmitFuture(Future):
    def get_id(self):
        return submit_get_id(self)


def submit_async(
    flux_handle,
    jobspec,
    priority=lib.FLUX_JOB_PRIORITY_DEFAULT,
    waitable=False,
    debug=False,
    pre_signed=False,
):
    """Ask Flux to run a job, without waiting for a response

    Submit a job to Flux.  This method returns immediately with a
    Flux Future, which can be used obtain the job ID later.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobspec: jobspec defining the job request
    :type jobspec: Jobspec or its string encoding
    :param priority: job priority 0 (lowest) through 31 (highest)
        (default is 16).  Priorities 0 through 15 are restricted to
        the instance owner.
    :type priority: int
    :param waitable: allow result to be fetched with job.wait()
        (default is False).  Waitable=True is restricted to the
        instance owner.
    :type waitable: bool
    :param debug: enable job manager debugging events to job eventlog
        (default is False)
    :type debug: bool
    :param pre_signed: jobspec argument is already signed
        (default is False)
    :type pre_signed: bool
    :returns: a Flux Future object for obtaining the assigned jobid
    :rtype: Future
    """
    jobspec = _convert_jobspec_arg_to_string(jobspec)
    flags = 0
    if waitable:
        flags |= constants.FLUX_JOB_WAITABLE
    if debug:
        flags |= constants.FLUX_JOB_DEBUG
    if pre_signed:
        flags |= constants.FLUX_JOB_PRE_SIGNED
    future_handle = RAW.submit(flux_handle, jobspec, priority, flags)
    return SubmitFuture(future_handle)


@check_future_error
def submit_get_id(future):
    """Get job ID from a Future returned by job.submit_async()

    Process a response to a Flux job submit request.  This method blocks
    until the response is received, then decodes the result to obtain
    the assigned job ID.

    :param future: a Flux future object returned by job.submit_async()
    :type future: Future
    :returns: job ID
    :rtype: int
    """
    if future is None or future == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "future must not be None/NULL")
    future.wait_for()  # ensure the future is fulfilled
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.submit_get_id(future, jobid)
    return int(jobid[0])


def submit(
    flux_handle,
    jobspec,
    priority=lib.FLUX_JOB_PRIORITY_DEFAULT,
    waitable=False,
    debug=False,
    pre_signed=False,
):
    """Submit a job to Flux

    Ask Flux to run a job, blocking until a job ID is assigned.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobspec: jobspec defining the job request
    :type jobspec: Jobspec or its string encoding
    :param priority: job priority 0 (lowest) through 31 (highest)
        (default is 16).  Priorities 0 through 15 are restricted to
        the instance owner.
    :type priority: int
    :param waitable: allow result to be fetched with job.wait()
        (default is False).  Waitable=true is restricted to the
        instance owner.
    :type waitable: bool
    :param debug: enable job manager debugging events to job eventlog
        (default is False)
    :type debug: bool
    :param pre_signed: jobspec argument is already signed
        (default is False)
    :type pre_signed: bool
    :returns: job ID
    :rtype: int
    """
    future = submit_async(flux_handle, jobspec, priority, waitable, debug, pre_signed)
    return future.get_id()


class JobWaitFuture(Future):
    def get_status(self):
        return wait_get_status(self)


def wait_async(flux_handle, jobid=lib.FLUX_JOBID_ANY):
    """Wait for a job to complete, asynchronously

    Submit a request to wait for job completion.  This method returns
    immediately with a Flux Future, which can be used to process
    the result later.

    Only jobs submitted with waitable=True can be waited for.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID to wait for (default is any waitable job)
    :returns: a Flux Future object for obtaining the job result
    :rtype: Future
    """
    future_handle = RAW.wait(flux_handle, jobid)
    return JobWaitFuture(future_handle)


JobWaitResult = collections.namedtuple("JobWaitResult", "jobid, success, errstr")


@check_future_error
def wait_get_status(future):
    """Get job status from a Future returned by job.wait_async()

    Process a response to a Flux job wait request.  This method blocks
    until the response is received, then decodes the result to obtain
    the job status.

    :param future: a Flux future object returned by job.wait_async()
    :type future: Future
    :returns: job status, a tuple of: Job ID (int), success (bool),
        and an error (string) if success=False
    :rtype: tuple
    """
    if future is None or future == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "future must not be None/NULL")
    future.wait_for()  # ensure the future is fulfilled
    success = ffi.new("bool[1]")
    errstr = ffi.new("const char *[1]")
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.wait_get_id(future, jobid)
    RAW.wait_get_status(future, success, errstr)
    return JobWaitResult(int(jobid[0]), bool(success[0]), ffi.string(errstr[0]))


def wait(flux_handle, jobid=lib.FLUX_JOBID_ANY):
    """Wait for a job to complete

    Submit a request to wait for job completion, blocking until a
    response is received, then return the job status.

    Only jobs submitted with waitable=True can be waited for.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID to wait for (default is any waitable job)
    :returns: job status, a tuple of: Job ID (int), success (bool),
        and an error (string) if success=False
    :rtype: tuple
    """
    future = wait_async(flux_handle, jobid)
    return future.get_status()


def kill_async(flux_handle, jobid, signum=None):
    """Send a signal to a running job asynchronously

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to kill
    :param signum: signal to send (default SIGTERM)
    :returns: a Future
    :rtype: Future
    """
    if not signum:
        signum = signal.SIGTERM
    return Future(RAW.kill(flux_handle, int(jobid), signum))


def kill(flux_handle, jobid, signum=None):
    """Send a signal to a running job.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to kill
    :param signum: signal to send (default SIGTERM)
    """
    return kill_async(flux_handle, jobid, signum).get()


def cancel_async(flux_handle, jobid, reason=None):
    """Cancel a pending or or running job asynchronously

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to cancel
    :returns: a Future
    :rtype: Future

    """
    if not reason:
        reason = ffi.NULL
    return Future(RAW.cancel(flux_handle, int(jobid), reason))


def cancel(flux_handle, jobid, signum=None):
    """Cancel a pending or or running job

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to cancel

    """
    return cancel_async(flux_handle, jobid, signum).get()


class EventLogEvent:
    """
    wrapper class for a single KVS EventLog entry
    """

    def __init__(self, event):
        """
        "Initialize from a string or dict eventlog event
        """
        if isinstance(event, str):
            event = json.loads(event)
        self._name = event["name"]
        self._timestamp = event["timestamp"]
        self._context = {}
        if "context" in event:
            self._context = event["context"]

    def __str__(self):
        return "{0.timestamp:<0.5f}: {0.name} {0.context}".format(self)

    @property
    def name(self):
        return self._name

    @property
    def timestamp(self):
        return self._timestamp

    @property
    def context(self):
        return self._context


class JobEventWatchFuture(Future):
    """
    A future returned from job.event_watch_async().
    Adds get_event() method to return an EventLogEntry event
    """

    def __del__(self):
        if self.needs_cancel is not False:
            self.cancel()
        try:
            super().__del__()
        except AttributeError:
            pass

    def __init__(self, future_handle):
        super().__init__(future_handle)
        self.needs_cancel = True

    def get_event(self, autoreset=True):
        """
        Return the next event from a JobEventWatchFuture, or None
        if the event stream has terminated.

        The future is auto-reset unless autoreset=False, so a subsequent
        call to get_event() will try to fetch the next event and thus
        may block.
        """
        result = ffi.new("char *[1]")
        try:
            RAW.event_watch_get(self.pimpl, result)
        except OSError as exc:
            if exc.errno == errno.ENODATA:
                self.needs_cancel = False
                return None
            # re-raise all other exceptions
            raise
        event = EventLogEvent(ffi.string(result[0]).decode("utf-8"))
        if autoreset is True:
            self.reset()
        return event

    def cancel(self):
        """Cancel a streaming job.event_watch_async() future
        """
        RAW.event_watch_cancel(self.pimpl)
        self.needs_cancel = False


def event_watch_async(flux_handle, jobid, eventlog="eventlog"):
    """Asynchronously get eventlog updates for a job

    Asynchronously watch the events of a job eventlog, optionally only
    returning events that match a glob pattern.

    Returns a JobEventWatchFuture. Call .get_event() from the then
    callback to get the currently returned event from the Future object.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID on which to watch events
    :param name: The event name or glob pattern for which to wait (default: \*)
    :param eventlog: eventlog path in job kvs directory (default: eventlog)
    :returns: a JobEventWatchFuture object
    :rtype: JobEventWatchFuture
    """

    future = RAW.event_watch(flux_handle, int(jobid), eventlog, 0)
    return JobEventWatchFuture(future)


def event_watch(flux_handle, jobid, eventlog="eventlog"):
    """Python generator to watch all events for a job

    Synchronously watch events a job eventlog via a simple generator.
    Use as::

        for event in job.event_watch(flux_handle, jobid):
            # do something with event...

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID on which to watch events
    :param name: The event name or glob pattern for which to wait (default: \*)
    :param eventlog: eventlog path in job kvs directory (default: eventlog)
    """
    watcher = event_watch_async(flux_handle, jobid, eventlog)
    event = watcher.get_event()
    while event is not None:
        yield event
        event = watcher.get_event()


class JobException(Exception):
    def __init__(self, event):
        self.timestamp = event.timestamp
        self.type = event.context["type"]
        self.note = event.context["note"]
        self.severity = event.context["severity"]
        super().__init__(self)

    def __str__(self):
        return f"job.exception: type={self.type}: {self.note}"


def event_wait(flux_handle, jobid, name, eventlog="eventlog", raiseJobException=True):
    """Wait for a job eventlog entry 'name'

    Wait synchronously for an eventlog entry named "name" and
    return the entry to caller, raises OSError with ENODATA if
    event never occurred

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID on which to wait for eventlog events
    :param name: The event name for which to wait
    :param eventlog: eventlog path in job kvs directory (default: eventlog)
    :param raiseJobException: if True, watch for job exception events and
      raise a JobException if one is seen before event 'name' (default=True)
    :returns: an EventLogEntry object, or raises OSError if eventlog
     ended before matching event was found
    :rtype: EventLogEntry
    """
    for event in event_watch(flux_handle, jobid, eventlog):
        if event.name == name:
            return event
        if (
            raiseJobException
            and event.name == "exception"
            and event.context["severity"] == 0
        ):
            raise JobException(event)
    raise OSError(errno.ENODATA, f"eventlog ended before event='{name}'")


class JobListRPC(RPC):
    def get_jobs(self):
        return self.get()["jobs"]


# Due to subtleties in the python bindings and this call, this binding
# is more of a reimplementation of flux_job_list() instead of calling
# the flux_job_list() C function directly.  Some reasons:
#
# - Desire to return a Python RPC class and use its get() method
# - Desired return value is json array, not a single value
#
# pylint: disable=dangerous-default-value
def job_list(
    flux_handle, max_entries=1000, attrs=[], userid=os.getuid(), states=0, results=0
):
    payload = {
        "max_entries": int(max_entries),
        "attrs": attrs,
        "userid": int(userid),
        "states": states,
        "results": results,
    }
    return JobListRPC(flux_handle, "job-info.list", payload)


def job_list_inactive(flux_handle, since=0.0, max_entries=1000, attrs=[], name=None):
    payload = {"since": float(since), "max_entries": int(max_entries), "attrs": attrs}
    if name:
        payload["name"] = name
    return JobListRPC(flux_handle, "job-info.list-inactive", payload)


class JobListIdRPC(RPC):
    def get_job(self):
        return self.get()["job"]


# list-id is not like list or list-inactive, it doesn't return an
# array, so don't use JobListRPC
def job_list_id(flux_handle, jobid, attrs=[]):
    payload = {"id": int(jobid), "attrs": attrs}
    return JobListIdRPC(flux_handle, "job-info.list-id", payload)


def _validate_keys(expected, given, keys_optional=False, allow_additional=False):
    if not isinstance(expected, set):
        expected = set(expected)
    if not isinstance(given, set):
        given = set(given)
    if not keys_optional:
        for req_key in expected.difference(given):
            raise ValueError("Missing key ({})".format(req_key))
    if not allow_additional:
        for extraneous_key in given.difference(expected):
            raise ValueError("Extraneous key ({})".format(extraneous_key))


def validate_jobspec(jobspec, require_version=None):
    """
    Validates the jobspec by attempting to construct a Jobspec object.  If no
    exceptions are thrown during construction, then the jobspec is assumed to be
    valid and this function returns True.  If the jobspec is invalid, the
    relevant exception is thrown (i.e., TypeError, ValueError, EnvironmentError)

    By default, the validation function will read the `version` key in the
    jobspec to determine which Jobspec object to instantiate. An optional
    `require_version` is included to override this behavior and force a
    particular class to be used.

    :param jobspec: a Jobspec object or JSON string
    :param require_version: jobspec version to use, if not provided,
                            the value of jobspec['version'] is used
    :raises ValueError:
    :raises TypeError:
    :raises EnvironmentError:
    """
    jobspec_str = _convert_jobspec_arg_to_string(jobspec)
    jobspec_obj = json.loads(jobspec_str)
    if jobspec_obj is None:
        return (1, "Unable to parse JSON")
    _validate_keys(Jobspec.top_level_keys, jobspec_obj.keys())
    if require_version == 1 or jobspec_obj.get("version", 0) == 1:
        JobspecV1(**jobspec_obj)
    else:
        Jobspec(**jobspec_obj)
    return True


class Jobspec(object):
    top_level_keys = set(["resources", "tasks", "version", "attributes"])

    def __init__(self, resources, tasks, **kwargs):
        """
        Constructor for Canonical Jobspec, as described in RFC 14

        :param resources:  dictionary following the specification in RFC14 for
                           the `resources` top-level key
        :param tasks: dictionary following the specification in RFC14 for the
                      `tasks` top-level key
        :param attributes: dictionary following the specification in RFC14 for
                           the `attributes` top-level key
        :param version: included to allow for usage like JobspecV1(**jobspec)
        :raises ValueError:
        :raises TypeError:
        """

        # ensure that no unknown keyword arguments are used
        _validate_keys(
            ["attributes", "version"],
            kwargs,
            keys_optional=False,
            allow_additional=False,
        )

        if "version" not in kwargs:
            raise ValueError("version must be set")
        version = kwargs["version"]
        attributes = kwargs.get("attributes", None)

        if not isinstance(resources, abc.Sequence):
            raise TypeError("resources must be a sequence")
        if not isinstance(tasks, abc.Sequence):
            raise TypeError("tasks must be a sequence")
        if not isinstance(version, int):
            raise TypeError("version must be an integer")
        if not isinstance(attributes, abc.Mapping):
            raise TypeError("attributes must be a mapping")
        if version < 1:
            raise ValueError("version must be >= 1")

        self.jobspec = {
            "resources": resources,
            "tasks": tasks,
            "attributes": attributes,
            "version": version,
        }

        for res in self:
            self._validate_resource(res)

        for task in tasks:
            self._validate_task(task)

        if attributes is not None:
            self._validate_attributes(attributes)

    @classmethod
    def from_yaml_stream(cls, yaml_stream):
        jobspec = yaml.safe_load(yaml_stream)
        _validate_keys(cls.top_level_keys, jobspec.keys())
        return cls(**jobspec)

    @classmethod
    def from_yaml_file(cls, filename):
        with open(filename, "rb") as infile:
            return cls.from_yaml_stream(infile)

    @staticmethod
    def _validate_complex_range(range_dict):
        if "min" not in range_dict:
            raise ValueError("min must be in range")
        if len(range_dict) > 1:
            _validate_keys(["min", "max", "operator", "operand"], range_dict.keys())
        for key in ["min", "max", "operand"]:
            if key not in range_dict:
                continue
            if not isinstance(range_dict[key], six.integer_types):
                raise TypeError("{} must be an int".format(key))
            if range_dict[key] < 1:
                raise ValueError("{} must be > 0".format(key))
        valid_operator_values = ["+", "*", "^"]
        if (
            "operator" in range_dict
            and range_dict["operator"] not in valid_operator_values
        ):
            raise ValueError("operator must be one of {}".format(valid_operator_values))

    @classmethod
    def _validate_resource(cls, res):
        if not isinstance(res, abc.Mapping):
            raise TypeError("resource must be a mapping")

        # validate the 'type' key
        if "type" not in res:
            raise ValueError("type is a required key for resources")
        if not isinstance(res["type"], six.string_types):
            raise TypeError("type must be a string")

        # validate the 'count' key
        if "count" not in res:
            raise ValueError("count is a required key for resources")
        count = res["count"]
        if isinstance(count, abc.Mapping):
            cls._validate_complex_range(count)
        elif not isinstance(count, six.integer_types):
            raise TypeError("count must be an int or mapping")
        elif count < 1:
            raise ValueError("count must be > 0")

        # validate the string keys
        for key in ["id", "unit", "label"]:
            if key in res and not isinstance(res[key], six.string_types):
                raise TypeError("{} must be a string".format(key))

        # validate the 'exclusive' key
        if "exclusive" in res:
            if res["exclusive"] not in [True, False]:
                raise TypeError("exclusive must be a boolean")

        # validate that slots have a 'label'
        if res["type"] == "slot" and "label" not in res:
            raise ValueError("slots must have labels")

    @staticmethod
    def _validate_task(task):
        if not isinstance(task, abc.Mapping):
            raise TypeError("task must be a mapping")

        _validate_keys(["command", "slot", "count"], task.keys(), allow_additional=True)

        if not isinstance(task["count"], abc.Mapping):
            raise TypeError("count must be a mapping")

        if not isinstance(task["slot"], six.string_types):
            raise TypeError("slot must be a string")

        if "attributes" in task and not isinstance(task["attributes"], abc.Mapping):
            raise TypeError("count must be a mapping")

        command = task["command"]
        if len(command) == 0:
            raise TypeError("command array cannot have length of zero")
        if not (
            (  # sequence of strings - N.B. also true for just a plain string
                isinstance(command, abc.Sequence)
                and all(isinstance(x, six.string_types) for x in command)
            )
        ) or isinstance(command, six.string_types):
            raise TypeError("command must be a list of strings")

    @staticmethod
    def _validate_attributes(attributes):
        _validate_keys(["system", "user"], attributes.keys(), keys_optional=True)

    @staticmethod
    def _create_resource(res_type, count, with_child=None):
        if with_child is not None and not isinstance(with_child, abc.Sequence):
            raise TypeError("child resource must None or a sequence")
        if with_child is not None and isinstance(with_child, six.string_types):
            raise TypeError("child resource must not be a string")
        if not count > 0:
            raise ValueError("resource count must be > 0")

        res = {"type": res_type, "count": count}

        if with_child:
            res["with"] = with_child
        return res

    @classmethod
    def _create_slot(cls, label, count, with_child):
        slot = cls._create_resource("slot", count, with_child)
        slot["label"] = label
        return slot

    @property
    def duration(self):
        try:
            return self.jobspec["attributes"]["system"]["duration"]
        except KeyError:
            return None

    @duration.setter
    def duration(self, duration):
        """
        Assign a time limit to the job.  The duration may be:
        - an int or float in seconds
        - a string in Flux Standard Duration
        - a python datetime.timedelta
        A duration of zero is interpreted as "not set".
        """
        if isinstance(duration, six.string_types):
            time = parse_fsd(duration)
        elif isinstance(duration, datetime.timedelta):
            time = duration.total_seconds()
        elif isinstance(duration, (float, int)):
            time = float(duration)
        else:
            raise TypeError("duration must be an int, float, string, or timedelta")
        if time < 0:
            raise ValueError("duration must not be negative")
        if math.isnan(time) or math.isinf(time):
            raise ValueError("duration must be a normal, finite value")
        self.setattr("system.duration", time)

    @property
    def cwd(self):
        """
        Get working directory of job.
        """
        try:
            return self.jobspec["attributes"]["system"]["cwd"]
        except KeyError:
            return None

    @cwd.setter
    def cwd(self, cwd):
        """
        Set working directory of job. The cwd may be:
        - a pathlib object (if py 3.6+)
        - a string
        """
        if six.PY3 and isinstance(cwd, os.PathLike):
            cwd = os.fspath(cwd)
        if not isinstance(cwd, six.string_types):
            raise ValueError("cwd must be a string")
        self.setattr("system.cwd", cwd)

    @property
    def environment(self):
        """
        Get (entire) environment of job.
        """
        try:
            return self.jobspec["attributes"]["system"]["environment"]
        except KeyError:
            return None

    @environment.setter
    def environment(self, environ):
        """
        Set (entire) environment of job.

        Does a direct assignment (i.e., no deep copy), so future modifications
        to the `environ` will be reflected in the jobspec.
        """
        if not isinstance(environ, abc.Mapping):
            raise ValueError("environment must be a mapping")
        self.setattr("system.environment", environ)

    @property
    def stdin(self):
        return self._get_io_path("input", "stdin")

    @stdin.setter
    def stdin(self, path):
        """Redirect stdin to a file given by `path`"""
        self._set_io_path("input", "stdin", path)

    @property
    def stdout(self):
        return self._get_io_path("output", "stdout")

    @stdout.setter
    def stdout(self, path):
        """Redirect stdout to a file given by `path`"""
        self._set_io_path("output", "stdout", path)

    @property
    def stderr(self):
        return self._get_io_path("output", "stderr")

    @stderr.setter
    def stderr(self, path):
        """Redirect stderr to a file given by `path`"""
        self._set_io_path("output", "stderr", path)

    def _get_io_path(self, iotype, stream_name):
        """Get the path of a stdio stream, if set.

        :param iotype: the stream type, one of `"input"` or `"output"`
        :param stream_name: the name of the io stream
        """
        try:
            return self.jobspec["attributes"]["system"]["shell"]["options"][iotype][
                stream_name
            ]["path"]
        except KeyError:
            return None

    def _set_io_path(self, iotype, stream_name, path):
        """Set the path of a stdio stream.

        :param iotype: the stream type, one of `"input"` or `"output"`
        :param stream_name: the name of the io stream
        :param path: the path to redirect the stream
        """
        self.setattr_shell_option("{}.{}.type".format(iotype, stream_name), "file")
        self.setattr_shell_option("{}.{}.path".format(iotype, stream_name), path)

    def _set_treedict(self, in_dict, key, val):
        """
        _set_treedict(d, "a.b.c", 42) is like d[a][b][c] = 42
        but levels are created on demand.
        """
        path = key.split(".", 1)
        if len(path) == 2:
            self._set_treedict(in_dict.setdefault(path[0], {}), path[1], val)
        else:
            in_dict[key] = val

    def setattr(self, key, val):
        """
        set job attribute
        """
        self._set_treedict(self.jobspec, "attributes." + key, val)

    def setattr_shell_option(self, key, val):
        """
        set job attribute: shell option
        """
        self.setattr("system.shell.options." + key, val)

    def dumps(self, **kwargs):
        return json.dumps(self.jobspec, ensure_ascii=False, **kwargs)

    @property
    def resources(self):
        return self.jobspec.get("resources", None)

    @property
    def tasks(self):
        return self.jobspec.get("tasks", None)

    @property
    def attributes(self):
        return self.jobspec.get("attributes", None)

    @property
    def version(self):
        return self.jobspec.get("version", None)

    def __iter__(self):
        """
        Iterate over the resources in the `resources` section of the jobspec.

        Performs a depth-first, pre-order traversal.
        """

        def iter_helper(res_list):
            for resource in res_list:
                yield resource
                children = resource.get("with", [])
                # PY2: convert to `yield from` after dropping 2.7
                for res in iter_helper(children):
                    yield res

        return iter_helper(self.resources)

    def resource_walk(self):
        """
        Traverse the resources in the ``resources`` section of the jobspec.

        Performs a depth-first, pre-order traversal. Yields a tuple containing
        (parent, resource, count).  ``parent`` is None when ``resource`` is a
        top-level resource. ``count`` is the number of that resource including the
        multiplicative effects of the ``with`` clause in ancestor resources.  For
        example, the following resource section, will yield a count of 2 for the
        ``slot`` and a count of 8 for the ``core`` resource.

        .. code-block:: yaml

            - type: slot
              count: 2
              with:
                - type: core
                  count: 4
        """

        def walk_helper(res_list, parent, count):
            for resource in res_list:
                res_count = count * resource["count"]
                yield (parent, resource, res_count)
                children = resource.get("with", [])
                # PY2: convert to `yield from` after dropping 2.7
                for walk_tuple in walk_helper(children, resource, res_count):
                    yield walk_tuple

        return walk_helper(self.resources, None, 1)

    def resource_counts(self):
        """
        Compute the counts of each resource type in the jobspec

        The following jobspec would return
        ``{ "slot": 12, "core": 18, "memory": 242 }``

        .. code-block:: yaml

            - type: slot
              count: 2
              with:
                - type: core
                  count: 4
                - type: memory
                  count: 1
                  unit: GB
            - type: slot
              count: 10
              with:
                - type: core
                  count: 1
                - type: memory
                  count: 24
                  unit: GB

        .. note::

            The current implementation ignores the ``unit`` label and assumes
            they are consist across resources
        """
        count_dict = collections.defaultdict(lambda: 0)
        for _, resource, count in self.resource_walk():
            count_dict[resource["type"]] += count
        return count_dict


class JobspecV1(Jobspec):
    def __init__(self, resources, tasks, **kwargs):
        """
        Constructor for Version 1 of the Jobspec

        :param resources:  dictionary following the specification in RFC14 for
                           the `resources` top-level key
        :param tasks: dictionary following the specification in RFC14 for the
                      `tasks` top-level key
        :param attributes: dictionary following the specification in RFC14 for
                           the `attributes` top-level key
        :param version: must be 1, included to allow for usage like
                        JobspecV1(**jobspec)
        """

        # ensure that no unknown keyword arguments are used
        _validate_keys(
            ["attributes", "version"],
            kwargs,
            keys_optional=True,
            allow_additional=False,
        )

        if "version" not in kwargs:
            kwargs["version"] = 1
        elif kwargs["version"] != 1:
            raise ValueError("version must be 1")

        super(JobspecV1, self).__init__(resources, tasks, **kwargs)

        # validate V1 specific requirements:
        self._v1_validate(resources, tasks, kwargs)

    @staticmethod
    def _v1_validate(resources, tasks, kwargs):
        # process extra V1 attributes requirements:

        # attributes already required by base Jobspec validator
        attributes = kwargs["attributes"]

        # attributes.system.duration is required
        if "system" not in attributes:
            raise ValueError("attributes.system is a required key")
        system = attributes["system"]
        if not isinstance(system, abc.Mapping):
            raise ValueError("attributes.system must be a mapping")
        if "duration" not in system:
            raise ValueError("attributes.system.duration is a required key")
        if not isinstance(system["duration"], numbers.Number):
            raise ValueError("attributes.system.duration must be a number")

    @classmethod
    def from_command(
        cls, command, num_tasks=1, cores_per_task=1, gpus_per_task=None, num_nodes=None
    ):
        """
        Factory function that builds the minimum legal v1 jobspec.

        Use setters to assign additional properties.

        :param command: command to execute
        :type command: iterable of str
        :param num_tasks: number of MPI tasks to create
        :param cores_per_task: number of cores to allocate per task
        :param gpus_per_task: number of GPUs to allocate per task
        :param num_nodes: distribute allocated tasks across N individual nodes
        """
        if not isinstance(num_tasks, int) or num_tasks < 1:
            raise ValueError("task count must be a integer >= 1")
        if not isinstance(cores_per_task, int) or cores_per_task < 1:
            raise ValueError("cores per task must be an integer >= 1")
        if gpus_per_task is not None:
            if not isinstance(gpus_per_task, int) or gpus_per_task < 0:
                raise ValueError("gpus per task must be an integer >= 0")
        if num_nodes is not None:
            if not isinstance(num_nodes, int) or num_nodes < 1:
                raise ValueError("node count must be an integer >= 1 (if set)")
            if num_nodes > num_tasks:
                raise ValueError("node count must not be greater than task count")
        children = [cls._create_resource("core", cores_per_task)]
        if gpus_per_task not in (None, 0):
            children.append(cls._create_resource("gpu", gpus_per_task))
        if num_nodes is not None:
            num_slots = int(math.ceil(num_tasks / float(num_nodes)))
            if num_tasks % num_nodes != 0:
                # N.B. uneven distribution results in wasted task slots
                task_count_dict = {"total": num_tasks}
            else:
                task_count_dict = {"per_slot": 1}
            slot = cls._create_slot("task", num_slots, children)
            resource_section = cls._create_resource("node", num_nodes, [slot])
        else:
            task_count_dict = {"per_slot": 1}
            slot = cls._create_slot("task", num_tasks, children)
            resource_section = slot

        resources = [resource_section]
        tasks = [{"command": command, "slot": "task", "count": task_count_dict}]
        attributes = {"system": {"duration": 0}}
        return cls(resources, tasks, attributes=attributes)

    @classmethod
    def from_batch_command(
        cls,
        script,
        jobname,
        args=None,
        num_slots=1,
        cores_per_slot=1,
        gpus_per_slot=None,
        num_nodes=None,
        broker_opts=None,
    ):
        """Create a Jobspec describing a nested Flux instance controlled by a script.

        The nested Flux instance will execute the script with the given
        command-line arguments after copying it and setting the executable bit.
        Conceptually, this differs from the `from_nest_command`, which also creates a
        nested Flux instance, in that it a) requires the initial program of the new
        instance to be an executable text file and b) creates the initial program
        from a string rather than using an executable existing somewhere on the
        filesystem.

        Use setters to assign additional properties.

        :param script: contents of the script to execute, as a string. The
            script should have a shebang (e.g. `#!/bin/sh`) at the top.
        :param jobname: name to use as the argv[0] for this job.
            This will be the default job name reported by Flux.
            (Note the actual argv is overridden by the job shell when executed.)
        :type jobname: str
        :param args: arguments to pass to `script`
        :type args: iterable of `str`
        :param num_slots: number of resource slots to create. Slots are an abstraction,
            and are only used (along with `cores_per_slot` and `gpus_per_slot`) to
            determine the nested instance's allocation size and layout.
        :param cores_per_slot: number of cores to allocate per slot
        :param gpus_per_slot: number of GPUs to allocate per slot
        :param num_nodes: distribute allocated resource slots across N individual nodes
        :param broker_opts: options to pass to the new Flux broker
        :type broker_opts: iterable of str
        """
        if not script.startswith("#!"):
            raise ValueError(f"{jobname} does not appear to start with '#!'")
        args = () if args is None else args
        jobspec = cls.from_command(
            command=[jobname, *args],  # argv[0] will be replaced with the script
            num_tasks=num_slots,
            cores_per_task=cores_per_slot,
            gpus_per_task=gpus_per_slot,
            num_nodes=num_nodes,
        )
        jobspec.setattr_shell_option("per-resource.type", "node")
        #  Copy script contents into jobspec
        jobspec.setattr("system.batch.script", script)
        if broker_opts is not None:
            jobspec.setattr("system.batch.broker-opts", broker_opts)
        return jobspec

    @classmethod
    def from_nest_command(
        cls,
        command,
        num_slots=1,
        cores_per_slot=1,
        gpus_per_slot=None,
        num_nodes=None,
        broker_opts=None,
    ):
        """Create a Jobspec describing a nested Flux instance controlled by `command`.

        Conceptually, this differs from the `from_batch_command` method in that a)
        the initial program of the nested Flux instance can be any executable
        on the file system, not just a text file and b) the executable is not
        copied at submission time.

        Use setters to assign additional properties.

        :param command: initial program for the nested Flux instance
        :type command: iterable of str
        :param num_slots: number of resource slots to create. Slots are an abstraction,
            and are only used (along with `cores_per_slot` and `gpus_per_slot`) to
            determine the nested instance's allocation size and layout.
        :param cores_per_slot: number of cores to allocate per slot
        :param gpus_per_slot: number of GPUs to allocate per slot
        :param num_nodes: distribute allocated resource slots across N individual nodes
        :param broker_opts: options to pass to the new Flux broker
        :type broker_opts: iterable of str
        """
        broker_opts = () if broker_opts is None else broker_opts
        jobspec = cls.from_command(
            command=["flux", "broker", *broker_opts, *command],
            num_tasks=num_slots,
            cores_per_task=cores_per_slot,
            gpus_per_task=gpus_per_slot,
            num_nodes=num_nodes,
        )
        jobspec.setattr_shell_option("per-resource.type", "node")
        return jobspec
