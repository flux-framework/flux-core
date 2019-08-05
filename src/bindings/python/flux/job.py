###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import math
import json
import errno
import collections

import six
import yaml

from flux.wrapper import Wrapper
from flux.util import check_future_error
from flux.future import Future
from _flux._core import ffi, lib

try:
    # pylint: disable=invalid-name
    collectionsAbc = collections.abc
except AttributeError:
    # pylint: disable=invalid-name
    collectionsAbc = collections


class JobWrapper(Wrapper):
    def __init__(self):
        super(JobWrapper, self).__init__(ffi, lib, prefixes=["flux_job_"])


RAW = JobWrapper()


def submit_async(flux_handle, jobspec, priority=lib.FLUX_JOB_PRIORITY_DEFAULT, flags=0):
    if isinstance(jobspec, six.text_type):
        jobspec = jobspec.encode("utf-8")
    elif jobspec is None or jobspec == ffi.NULL:
        # catch this here rather than in C for a better error message
        raise EnvironmentError(errno.EINVAL, "jobspec must not be None/NULL")
    elif not isinstance(jobspec, six.binary_type):
        raise TypeError("jobpsec must be a string (either binary or unicode)")

    future_handle = RAW.submit(flux_handle, jobspec, priority, flags)
    return Future(future_handle)


@check_future_error
def submit_get_id(future):
    if future is None or future == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "future must not be None/NULL")
    future.wait_for()  # ensure the future is fulfilled
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.submit_get_id(future, jobid)
    return int(jobid[0])


def submit(flux_handle, jobspec, priority=lib.FLUX_JOB_PRIORITY_DEFAULT, flags=0):
    future = submit_async(flux_handle, jobspec, priority, flags)
    jid = submit_get_id(future)
    return jid


def wait_async(flux_handle, jobid=lib.FLUX_JOBID_ANY):
    future_handle = RAW.wait(flux_handle, jobid)
    return Future(future_handle)


JobWaitResult = collections.namedtuple("JobWaitResult", "jobid, success, errstr")


@check_future_error
def wait_get_status(future):
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
    future = wait_async(flux_handle, jobid)
    status = wait_get_status(future)
    return status


class JobSpec:
    def __init__(
        self, command, num_tasks=1, cores_per_task=1, gpus_per_task=None, num_nodes=None
    ):
        """
        Constructor builds the minimum legal v1 jobspec.
        Use setters to assign additional properties.
        """
        if not isinstance(command, (list, tuple)) or not command:
            raise ValueError("command must be a non-empty list or tuple")
        if not isinstance(num_tasks, int) or num_tasks < 1:
            raise ValueError("task count must be a integer >= 1")
        if not isinstance(cores_per_task, int) or cores_per_task < 1:
            raise ValueError("cores per task must be an integer >= 1")
        if gpus_per_task is not None:
            if not isinstance(gpus_per_task, int) or gpus_per_task < 1:
                raise ValueError("gpus per task must be an integer >= 1")
        if num_nodes is not None:
            if not isinstance(num_nodes, int) or num_nodes < 1:
                raise ValueError("node count must be an integer >= 1 (if set)")
            if num_nodes > num_tasks:
                raise ValueError("node count must not be greater than task count")
        children = [self.__create_resource("core", cores_per_task)]
        if gpus_per_task is not None:
            children.append(self.__create_resource("gpu", gpus_per_task))
        if num_nodes is not None:
            num_slots = int(math.ceil(num_tasks / float(num_nodes)))
            if num_tasks % num_nodes != 0:
                # N.B. uneven distribution results in wasted task slots
                task_count_dict = {"total": num_tasks}
            else:
                task_count_dict = {"per_slot": 1}
            slot = self.__create_slot("task", num_slots, children)
            resource_section = self.__create_resource("node", num_nodes, [slot])
        else:
            task_count_dict = {"per_slot": 1}
            slot = self.__create_slot("task", num_tasks, children)
            resource_section = slot

        self.jobspec = {
            "version": 1,
            "resources": [resource_section],
            "tasks": [{"command": command, "slot": "task", "count": task_count_dict}],
            "attributes": {"system": {"duration": 0}},
        }

    def __create_resource(self, res_type, count, with_child=[]):
        assert isinstance(
            with_child, collectionsAbc.Sequence
        ), "child resource must be a sequence"
        assert not isinstance(
            with_child, six.string_types
        ), "child resource must not be a string"
        assert count > 0, "resource count must be > 0"

        res = {"type": res_type, "count": count}

        if len(with_child) > 0:
            res["with"] = with_child
        return res

    def __create_slot(self, label, count, with_child):
        slot = self.__create_resource("slot", count, with_child)
        slot["label"] = label
        return slot

    def __parse_fsd(self, s):
        m = re.match(r".*([smhd])$", s)
        try:
            n = float(s[:-1] if m else s)
        except:
            raise ValueError("invalid Flux standard duration")
        unit = m.group(1) if m else "s"

        if unit == "m":
            seconds = timedelta(minutes=n).total_seconds()
        elif unit == "h":
            seconds = timedelta(hours=n).total_seconds()
        elif unit == "d":
            seconds = timedelta(days=n).total_seconds()
        else:
            seconds = n
        if seconds < 0 or math.isnan(seconds) or math.isinf(seconds):
            raise ValueError("invalid Flux standard duration")
        return seconds

    def set_duration(self, duration):
        """
        Assign a time limit to the job.  The duration may be:
        - a float in seconds
        - a string in Flux Standard Duration
        A duration of zero is interpreted as "not set".
        """
        if isinstance(duration, six.string_types):
            time = self.__parse_fsd(duration)
        elif isinstance(duration, float):
            time = duration
        else:
            raise ValueError("duration must be a float or string")
        if time < 0:
            raise ValueError("duration must not be negative")
        if math.isnan(time) or math.isinf(time):
            raise ValueError("duration must be a normal, finite value")
        self.jobspec["attributes"]["system"]["duration"] = time

    def set_cwd(self, cwd):
        """
        Set working directory of job.
        """
        if not isinstance(cwd, six.string_types):
            raise ValueError("cwd must be a string")
        self.jobspec["attributes"]["system"]["cwd"] = cwd

    def set_environment(self, environ):
        """
        Set (entire) environment of job.
        """
        if not isinstance(environ, collectionsAbc.Mapping):
            raise ValueError("environment must be a mapping")
        self.jobspec["attributes"]["system"]["environment"] = environ

    def __set_treedict(self, in_dict, key, val):
        """
        __set_treedict(d, "a.b.c", 42) is like d[a][b][c] = 42
        but levels are created on demand.
        """
        path = key.split(".", 1)
        if len(path) == 2:
            self.__set_treedict(in_dict.setdefault(path[0], {}), path[1], val)
        else:
            in_dict[key] = val

    def setattr(self, key, val):
        """
        set job attribute
        """
        self.__set_treedict(self.jobspec, "attributes." + key, val)

    def setattr_shopt(self, key, val):
        """
        set job attribute: shell option
        """
        self.setattr("system.shell.options." + key, val)

    def dumps(self):
        return json.dumps(self.jobspec)
