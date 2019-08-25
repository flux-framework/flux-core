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

import os
import math
import json
import errno
import datetime
import collections

import six
import yaml

import flux.constants
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
    if isinstance(jobspec, Jobspec):
        jobspec = jobspec.dumps()
    elif isinstance(jobspec, six.text_type):
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
        """

        # ensure that no unknown keyword arguments are used
        _validate_keys(
            ["attributes", "version"],
            kwargs,
            keys_optional=True,
            allow_additional=False,
        )

        if "version" not in kwargs:
            raise ValueError("version must be set")
        version = kwargs["version"]
        attributes = kwargs.get("attributes", None)

        if not isinstance(resources, collectionsAbc.Sequence):
            raise TypeError("resources must be a sequence")
        if not isinstance(tasks, collectionsAbc.Sequence):
            raise TypeError("tasks must be a sequence")
        if not isinstance(version, int):
            raise TypeError("version must be an integer")
        if attributes is not None and not isinstance(
            attributes, collectionsAbc.Mapping
        ):
            raise TypeError("attributes must be a mapping")
        elif version < 1:
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
            elif range_dict[key] < 1:
                raise ValueError("{} must be > 0".format(key))
        valid_operator_values = ["+", "*", "^"]
        if (
            "operator" in range_dict
            and range_dict["operator"] not in valid_operator_values
        ):
            raise ValueError("operator must be one of {}".format(valid_operator_values))

    @classmethod
    def _validate_resource(cls, res):
        if not isinstance(res, collectionsAbc.Mapping):
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
        if isinstance(count, collectionsAbc.Mapping):
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
        if not isinstance(task, collectionsAbc.Mapping):
            raise TypeError("task must be a mapping")

        _validate_keys(["command", "slot", "count"], task.keys(), allow_additional=True)

        if not isinstance(task["count"], collectionsAbc.Mapping):
            raise TypeError("count must be a mapping")

        if not isinstance(task["slot"], six.string_types):
            raise TypeError("slot must be a string")

        if "attributes" in task and not isinstance(
            task["attributes"], collectionsAbc.Mapping
        ):
            raise TypeError("count must be a mapping")

        command = task["command"]
        if not (
            (  # sequence of strings - N.B. also true for just a plain string
                isinstance(command, collectionsAbc.Sequence)
                and all(isinstance(x, six.string_types) for x in command)
            )
        ) or isinstance(command, six.string_types):
            raise TypeError("command must be a list of strings")

    @staticmethod
    def _validate_attributes(attributes):
        _validate_keys(["system", "user"], attributes.keys(), keys_optional=True)

    @staticmethod
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
        - a float in seconds
        - a string in Flux Standard Duration
        - a python datetime.timedelta
        A duration of zero is interpreted as "not set".
        """
        if isinstance(duration, six.string_types):
            time = parse_fsd(duration)
        elif isinstance(duration, datetime.timedelta):
            time = duration.total_seconds()
        elif isinstance(duration, float):
            time = duration
        else:
            raise ValueError("duration must be a float, string, or timedelta")
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
        if not isinstance(environ, collectionsAbc.Mapping):
            raise ValueError("environment must be a mapping")
        self.setattr("system.environment", environ)

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
        return json.dumps(self.jobspec, **kwargs)

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
        Traverse the resources in the `resources` section of the jobspec.

        Performs a depth-first, pre-order traversal. Yields a tuple containing
        (parent, resource, count).  `parent` is None when `resource` is a
        top-level resource. `count` is the number of that resource including the
        multiplicative effects of the `with` clause in ancestor resources.  For
        example, the following resource section, will yield a count of 2 for the
        `slot` and a count of 8 for the `core` resource.

        ```yaml
        - type: slot
          count: 2
          with:
            - type: core
              count: 4
        ```
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
        `{ "slot": 12, "core": 18, "memory": 242 }`

        ```yaml
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
        ```

        Note: the current implementation ignores the `unit` label and assumes
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
        children = [cls._create_resource("core", cores_per_task)]
        if gpus_per_task is not None:
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

def convert_id(jobid, src="dec", dst="dec"):
    valid_id_types = six.string_types + six.integer_types
    if not any((isinstance(jobid, id_type) for id_type in valid_id_types)):
        raise TypeError("Jobid must be an integer or string, not {}".format(type(jobid)))

    valid_formats = ("dec", "hex", "kvs", "words")
    if src not in valid_formats:
        raise EnvironmentError(errno.EINVAL, "src must be one of {}", valid_formats)
    if dst not in valid_formats:
        raise EnvironmentError(errno.EINVAL, "dst must be one of {}", valid_formats)

    if isinstance(jobid, six.text_type):
        jobid = jobid.encode('utf-8')

    if src == dst:
        return jobid

    dec_jobid = ffi.new('uint64_t [1]') # uint64_t*
    if src == "dec":
        dec_jobid = jobid
    elif src == "hex":
        if (lib.fluid_decode (jobid, dec_jobid, flux.constants.FLUID_STRING_DOTHEX) < 0):
            raise EnvironmentError(errno.EINVAL, "malformed jobid: {}".format(src));
        dec_jobid = dec_jobid[0]
    elif src == "kvs":
        if jobid[0:4] != 'job.':
            raise EnvironmentError(errno.EINVAL, "missing 'job.' prefix")
        if (lib.fluid_decode (jobid[4:], dec_jobid, flux.constants.FLUID_STRING_DOTHEX) < 0):
            raise EnvironmentError(errno.EINVAL, "malformed jobid: {}".format(src));
        dec_jobid = dec_jobid[0]
    elif src == "words":
        if (lib.fluid_decode (jobid, dec_jobid, flux.constants.FLUID_STRING_MNEMONIC) < 0):
            raise EnvironmentError(errno.EINVAL, "malformed jobid: {}".format(src));
        dec_jobid = dec_jobid[0]


    buf_size = 64
    buf = ffi.new('char []', buf_size)
    def encode(id_format):
        pass

    if dst == 'dec':
        return dec_jobid
    elif dst == 'kvs':
        key_len = RAW.flux_job_kvs_key(buf, buf_size, dec_jobid, ffi.NULL)
        if key_len < 0:
            raise RuntimeError("error enconding id")
        return ffi.string(buf, key_len).decode('utf-8')
    elif dst == 'hex':
        if (lib.fluid_encode (buf, buf_size, dec_jobid, flux.constants.FLUID_STRING_DOTHEX) < 0):
            raise RuntimeError("error enconding id")
        return ffi.string(buf).decode('utf-8')
    elif dst == 'words':
        if (lib.fluid_encode (buf, buf_size, dec_jobid, flux.constants.FLUID_STRING_MNEMONIC) < 0):
            raise RuntimeError("error enconding id")
        return ffi.string(buf).decode('utf-8')
