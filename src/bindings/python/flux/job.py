###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
from __future__ import print_function

import math
import json
import errno
import collections

import six
import yaml

from flux.wrapper import Wrapper
from flux.util import check_future_error, parse_fsd
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


class Jobspec(object):
    def __init__(self, resources, tasks, version, attributes=None):
        if not isinstance(resources, collectionsAbc.Sequence):
            raise TypeError("resources must be a sequence")
        if not isinstance(tasks, collectionsAbc.Sequence):
            raise TypeError("tasks must be a sequence")
        if not isinstance(attributes, collectionsAbc.Sequence):
            raise TypeError("attributes must be a sequence")
        if not isinstance(version, int):
            raise TypeError("version must be an integer")
        elif version < 1:
            raise ValueError("version must be >= 1")

        self.jobspec = {
            "resources": resources,
            "tasks": tasks,
            "attributes": attributes,
            "version": version,
        }

    @classmethod
    def from_yaml_stream(cls, yaml_stream):
        jobspec = yaml.safe_load(yaml_stream)
        return cls(**jobspec)

    @classmethod
    def from_yaml_file(cls, filename):
        with open(filename, "rb") as infile:
            return cls.from_yaml_stream(infile)

    def _create_resource(res_type, count, with_child=None):
        if with_child is not None and not isinstance(
            with_child, collectionsAbc.Sequence
        ):
            raise TypeError("child resource must None or a sequence")
        elif with_child is not None and isinstance(with_child, six.string_types):
            raise TypeError("child resource must not be a string")
        if not count > 0:
            raise ValueError("resource count must be > 0")

        res = {"type": res_type, "count": count}

        if with_child:
            res["with"] = with_child
        return res

    def _create_slot(self, label, count, with_child):
        slot = self._create_resource("slot", count, with_child)
        slot["label"] = label
        return slot

    def set_duration(self, duration):
        """
        Assign a time limit to the job.  The duration may be:
        - a float in seconds
        - a string in Flux Standard Duration
        A duration of zero is interpreted as "not set".
        """
        if isinstance(duration, six.string_types):
            time = parse_fsd(duration)
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

    def setattr_shopt(self, key, val):
        """
        set job attribute: shell option
        """
        self.setattr("system.shell.options." + key, val)

    def dumps(self):
        return json.dumps(self.jobspec)


class JobspecV1(Jobspec):
    def __init__(self, resources, tasks, attributes=None, version=1):
        """
        Constructor for Version 1 of the Jobspec

        :param version: included to allow for usage like JobspecV1(**jobspec)
        """
        if version != 1:
            raise ValueError("version must be 1")
        super(JobspecV1, self).__init__(
            resources, tasks, version=version, attributes=attributes
        )

    @classmethod
    def from_command(
        cls, command, num_tasks=1, cores_per_task=1, gpus_per_task=None, num_nodes=None
    ):
        """
        Factory function that builds the minimum legal v1 jobspec.

        Use setters to assign additional properties.
        """
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
        children = [self._create_resource("core", cores_per_task)]
        if gpus_per_task is not None:
            children.append(self._create_resource("gpu", gpus_per_task))
        if num_nodes is not None:
            num_slots = int(math.ceil(num_tasks / float(num_nodes)))
            if num_tasks % num_nodes != 0:
                # N.B. uneven distribution results in wasted task slots
                task_count_dict = {"total": num_tasks}
            else:
                task_count_dict = {"per_slot": 1}
            slot = self._create_slot("task", num_slots, children)
            resource_section = self._create_resource("node", num_nodes, [slot])
        else:
            task_count_dict = {"per_slot": 1}
            slot = self._create_slot("task", num_tasks, children)
            resource_section = slot

        resources = [resource_section]
        tasks = [{"command": command, "slot": "task", "count": task_count_dict}]
        attributes = {"system": {"duration": 0}}
        return cls(resources, tasks, attributes=attributes)
