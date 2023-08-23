###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import collections
import collections.abc as abc
import datetime
import errno
import json
import math
import numbers
import os

import yaml
from _flux._core import ffi
from flux import hostlist, idset
from flux.util import Fileref, parse_fsd, set_treedict


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
    elif isinstance(jobspec, str):
        jobspec = jobspec.encode("utf-8", errors="surrogateescape")
    elif jobspec is None or jobspec == ffi.NULL:
        # catch this here rather than in C for a better error message
        raise EnvironmentError(errno.EINVAL, "jobspec must not be None/NULL")
    elif not isinstance(jobspec, bytes):
        raise TypeError(
            "jobspec must be a Jobspec or string (either binary or unicode)"
        )
    return jobspec


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


def _validate_dependency(dep):
    _validate_keys(["scheme", "value"], dep.keys(), allow_additional=True)
    if not isinstance(dep["scheme"], str):
        raise TypeError("dependency scheme must be a string")
    if not isinstance(dep["value"], str):
        raise TypeError("dependency value must be a string")


def _validate_property_query(name):
    invalid_chars = set("&'\"`|()")
    if any((x in invalid_chars) for x in name):
        raise TypeError(f"invalid character in property '{name}'")


def _validate_constraint_op(operator, args):
    if not isinstance(operator, str):
        raise TypeError(f"constraint operation {operator} is not a string")
    if not isinstance(args, abc.Sequence):
        raise TypeError(f"argument to constraint {operator} must be a sequence")
    if operator in ["and", "or", "not"]:
        for constraint in args:
            _validate_constraint(constraint)
    elif operator in ["properties"]:
        for name in args:
            _validate_property_query(name)
    elif operator in ["hostlist"]:
        for hosts in args:
            hostlist.decode(hosts)
    elif operator in ["ranks"]:
        for ranks in args:
            idset.decode(ranks)
    else:
        raise TypeError(f"unknown constraint operator '{operator}'")


def _validate_constraint(constraints):
    """Validate RFC 31 Constraint object"""
    if not isinstance(constraints, abc.Mapping):
        raise TypeError("constraints must be a mapping")
    for operator, arg in constraints.items():
        _validate_constraint_op(operator, arg)


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

            if "system" in attributes:
                self._validate_system_attributes(attributes["system"])

    @classmethod
    def from_yaml_stream(cls, yaml_stream):
        """Create a jobspec from a YAML file-like object."""
        jobspec = yaml.safe_load(yaml_stream)
        _validate_keys(cls.top_level_keys, jobspec.keys())
        return cls(**jobspec)

    @classmethod
    def from_yaml_file(cls, filename):
        """Create a jobspec from a path to a YAML file."""
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
            if not isinstance(range_dict[key], int):
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
    # pylint: disable=too-many-branches
    def _validate_resource(cls, res):
        if not isinstance(res, abc.Mapping):
            raise TypeError("resource must be a mapping")

        # validate the 'type' key
        if "type" not in res:
            raise ValueError("type is a required key for resources")
        if not isinstance(res["type"], str):
            raise TypeError("type must be a string")

        # validate the 'count' key
        if "count" not in res:
            raise ValueError("count is a required key for resources")
        count = res["count"]
        if isinstance(count, abc.Mapping):
            cls._validate_complex_range(count)
        elif not isinstance(count, int):
            raise TypeError("count must be an int or mapping")
        else:
            # node, slot, and core must have count > 0, but allow 0 for
            # any other resource type.
            if res["type"] in ["node", "slot", "core"] and count < 1:
                raise ValueError("node or slot count must be > 0")
            if count < 0:
                raise ValueError("count must be >= 0")

        # validate the string keys
        for key in ["id", "unit", "label"]:
            if key in res and not isinstance(res[key], str):
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

        count = task["count"]
        if "total" in count:
            if not isinstance(count["total"], int):
                raise TypeError("count total must be an int")
            if count["total"] <= 0:
                raise ValueError("count total must be > 0")
        if "per_slot" in count:
            if not isinstance(count["per_slot"], int):
                raise TypeError("count per_slot must be an int")
            if count["per_slot"] <= 0:
                raise ValueError("count per_slot must be > 0")

        if not isinstance(task["slot"], str):
            raise TypeError("slot must be a string")

        if "attributes" in task and not isinstance(task["attributes"], abc.Mapping):
            raise TypeError("count must be a mapping")

        command = task["command"]
        if len(command) == 0:
            raise TypeError("command array cannot have length of zero")
        if not (
            (  # sequence of strings - N.B. also true for just a plain string
                isinstance(command, abc.Sequence)
                and all(isinstance(x, str) for x in command)
            )
        ) or isinstance(command, str):
            raise TypeError("command must be a list of strings")

    @staticmethod
    def _validate_attributes(attributes):
        _validate_keys(["system", "user"], attributes.keys(), keys_optional=True)

    @staticmethod
    def _validate_system_attributes(system):
        if "dependencies" in system:
            if not isinstance(system["dependencies"], abc.Sequence):
                raise TypeError("attributes.system.dependencies must be a list")
            for dependency in system["dependencies"]:
                _validate_dependency(dependency)
        if "constraints" in system:
            _validate_constraint(system["constraints"])

    @staticmethod
    def _create_resource(res_type, count, with_child=None, exclusive=False):
        if with_child is not None and not isinstance(with_child, abc.Sequence):
            raise TypeError("child resource must None or a sequence")
        if with_child is not None and isinstance(with_child, str):
            raise TypeError("child resource must not be a string")
        if not count > 0:
            raise ValueError("resource count must be > 0")

        res = {"type": res_type, "count": count}

        if exclusive:
            res["exclusive"] = True

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
        """Job's time limit.

        The duration may be:

        * an int or float in seconds
        * a string in Flux Standard Duration (see RFC 23)
        * a python ``datetime.timedelta``

        A duration of zero is interpreted as "not set".
        """
        try:
            return self.jobspec["attributes"]["system"]["duration"]
        except KeyError:
            return None

    @duration.setter
    def duration(self, duration):
        """Assign a time limit to the job."""
        if isinstance(duration, str):
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
    def queue(self):
        """
        Target queue of job submission
        """
        try:
            return self.jobspec["attributes"]["system"]["queue"]
        except KeyError:
            return None

    @queue.setter
    def queue(self, queue):
        """
        Set target submission queue
        """
        if not isinstance(queue, str):
            raise TypeError("queue must be a string")
        self.setattr("system.queue", queue)

    @property
    def cwd(self):
        """
        Working directory of job.
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
        if isinstance(cwd, os.PathLike):
            cwd = os.fspath(cwd)
        if not isinstance(cwd, str):
            raise ValueError("cwd must be a string")
        self.setattr("system.cwd", cwd)

    @property
    def environment(self):
        """
        Environment of job. Defaults to ``None``.
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
        """Path to use for stdin."""
        return self._get_io_path("input", "stdin")

    @stdin.setter
    def stdin(self, path):
        """Redirect stdin to a file given by `path`, a string or pathlib object."""
        self._set_io_path("input", "stdin", path)

    @property
    def stdout(self):
        """Path to use for stdout."""
        return self._get_io_path("output", "stdout")

    @stdout.setter
    def stdout(self, path):
        """Redirect stdout to a file given by `path`, a string or pathlib object."""
        self._set_io_path("output", "stdout", path)

    @property
    def stderr(self):
        """Path to use for stderr."""
        return self._get_io_path("output", "stderr")

    @stderr.setter
    def stderr(self, path):
        """Redirect stderr to a file given by `path`, a string or pathlib object."""
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
        if isinstance(path, os.PathLike):
            path = os.fspath(path)
        if not isinstance(path, str):
            raise TypeError(
                "The path must be a string or pathlib object, "
                f"got {type(path).__name__}"
            )
        self.setattr_shell_option("{}.{}.type".format(iotype, stream_name), "file")
        self.setattr_shell_option("{}.{}.path".format(iotype, stream_name), path)

    def add_file(self, path, data, perms=0o0600, encoding=None):
        """
        Add a file to the RFC 14 "files" dictionary in Jobspec. If
        ``data`` contains newlines or an encoding is explicitly provided,
        then it is presumed to be the file content. Otherwise, ``data``
        is a local filesystem path, the contents of which are to be loaded
        into jobspec. For filesystem

        Args:
            path (str): path or file name to encode ``data`` as in Jobspec
            data (dict, str): content of file or a local path to load
            perms (int): file pemissions, default 0o0600 (octal). If ``data``
                is a file system path, then permissions of the local file
                system object will be used.
            encoding (str): RFC 37 compatible encoding for ``data``. None
                if ``data`` is a dict or to determine encoding from a file
                when ``data`` specifies a filesystem path.  O/w, if encoding
                set, data is a string encoded in specified ``encoding``.
        """
        if not (isinstance(data, abc.Mapping) or isinstance(data, str)):
            raise TypeError("data must be a Mapping or string")

        files = self.jobspec["attributes"]["system"].get("files", {})
        if "\n" in data and encoding is None:
            #  Use default encoding of utf-8 if data contains newlines,
            #  since this is presumed to be file content.
            encoding = "utf-8"
        files[path] = Fileref(data, perms=perms, encoding=encoding)
        self.jobspec["attributes"]["system"]["files"] = files

    def getattr(self, key):
        """
        get attribute from jobspec using dotted key notation, e.g.
        system.duration or optionally attributes.system.duration.

        Raises KeyError if a component of key does not exist.
        """
        if not key.startswith("attributes."):
            key = "attributes." + key
        value = self.jobspec
        for attr in key.split("."):
            value = value.get(attr)
            if value is None:
                raise KeyError
        return value

    def setattr(self, key, val):
        """
        set job attribute
        """
        if not key.startswith("attributes."):
            key = "attributes." + key
        set_treedict(self.jobspec, key, val)

    def setattr_shell_option(self, key, val):
        """
        set job attribute: shell option
        """
        self.setattr("system.shell.options." + key, val)

    def dumps(self, **kwargs):
        return json.dumps(self.jobspec, ensure_ascii=False, **kwargs)

    @property
    def resources(self):
        """Jobspec resources section"""
        return self.jobspec.get("resources", None)

    @property
    def tasks(self):
        """Jobspec tasks section"""
        return self.jobspec.get("tasks", None)

    @property
    def attributes(self):
        """Jobspec attributes section"""
        return self.jobspec.get("attributes", None)

    @property
    def version(self):
        """Jobspec version section"""
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
        `slot` and a count of 8 for the `core` resource:

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

        Note:
            the current implementation ignores the `unit` label and assumes
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
    # pylint: disable=too-many-branches,too-many-locals,too-many-statements
    def per_resource(
        cls,
        command,
        ncores=None,
        nnodes=None,
        per_resource_type=None,
        per_resource_count=None,
        gpus_per_node=None,
        exclusive=False,
    ):
        """
        Factory function that builds a v1 jobspec from an explicit count
        of nodes or cores and a number of tasks per one of these resources.

        Use setters to assign additional properties.

        Args:
            ncores: Total number of cores to allocate
            nnodes: Total number of nodes to allocate
            per_resource_type: (optional) Type of resource over which to
                               schedule a count of tasks. Only "node" or
                               "core" are currently supported.
            per_resource_count: (optional) Count of tasks per
                                `per_resource_type`
            gpus_per_node: With nnodes, request a number of gpus per node
            exclusive: with nnodes, request whole nodes exclusively
        """

        #  Handle per-resource specification:
        #  It is an error to specify one of per_resource_{type,count} and
        #   not the other:
        #
        per_resource = None
        if per_resource_type is not None and per_resource_count is not None:
            if not isinstance(per_resource_type, str):
                raise ValueError("per_resource_type must be a string")
            if per_resource_type not in ("node", "core"):
                raise ValueError(
                    f"Invalid per_resource_type='{per_resource_type}' specified"
                )
            if not isinstance(per_resource_count, int):
                raise ValueError("per_resource_count must be an integer")
            if per_resource_count < 1:
                raise ValueError("per_resource_count must be >= 1")

            per_resource = {"type": per_resource_type, "count": per_resource_count}
        elif per_resource_type is not None:
            raise ValueError("must specify a per_resource_count with per_resource_type")
        elif per_resource_count is not None:
            raise ValueError("must specify a per_resource_type with per_resource_count")

        if ncores is not None:
            if not isinstance(ncores, int) or ncores < 1:
                raise ValueError("ncores must be an integer >= 1")
        if gpus_per_node is not None:
            if not isinstance(gpus_per_node, int) or gpus_per_node < 0:
                raise ValueError("gpus_per_node must be an integer >= 0")
            if not nnodes:
                raise ValueError("gpus_per_node must be specified with nnodes")
        if nnodes is not None:
            if not isinstance(nnodes, int) or nnodes < 1:
                raise ValueError("nnodes must be an integer >= 1")
        elif exclusive:
            raise ValueError("exclusive can only be set with a node count")

        nslots = None
        slot_size = 1
        if nnodes and ncores:
            #  Request ncores across nnodes, actually running a given
            #   number of tasks per node or core
            if ncores < nnodes:
                raise ValueError("number of cores cannot be less than nnodes")
            if ncores % nnodes != 0:
                raise ValueError(
                    "number of cores must be evenly divisible by node count"
                )
            #
            #  With nnodes, nslots is slots/node (total_slots=slots*nodes)
            nslots = 1
            slot_size = int(ncores / nnodes)
        elif ncores:
            #  Request ncores total, actually running a given
            #   number of tasks per node or core
            #
            #  Without nnodes, nslots is total number of slots:
            nslots = ncores
            slot_size = 1
        elif nnodes:
            #  Request nnodes total with a given number of tasks per node
            #   or per core. (requires exclusive)
            if not exclusive:
                raise ValueError(
                    "Specifying nnodes also requires ncores or exclusive",
                )
            #  With nnodes, nslots is slots/node (total_slots=slots*nodes)
            nslots = 1
            slot_size = 1
        else:
            raise ValueError("must specify node or core count with per_resource")

        children = [cls._create_resource("core", slot_size)]
        if gpus_per_node:
            children.append(cls._create_resource("gpu", gpus_per_node))

        slot = cls._create_slot("task", nslots, children)

        if nnodes:
            resources = cls._create_resource("node", nnodes, [slot], exclusive)
        else:
            resources = slot

        resources = [resources]
        tasks = [{"command": command, "slot": "task", "count": {"per_slot": 1}}]
        attributes = {"system": {"duration": 0}}
        if per_resource:
            set_treedict(attributes, "system.shell.options.per-resource", per_resource)
        return cls(resources, tasks, attributes=attributes)

    @classmethod
    # pylint: disable=too-many-branches
    def from_command(
        cls,
        command,
        num_tasks=1,
        cores_per_task=1,
        gpus_per_task=None,
        num_nodes=None,
        exclusive=False,
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
        elif exclusive:
            raise ValueError("exclusive can only be set with a node count")
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
            resource_section = cls._create_resource(
                "node", num_nodes, [slot], exclusive
            )
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
        exclusive=False,
        conf=None,
    ):
        """
        Create a Jobspec describing a nested Flux instance controlled by
        a script.

        The nested Flux instance will execute the script with the given
        command-line arguments after copying it and setting the executable
        bit.  Conceptually, this differs from the `from_nest_command`,
        which also creates a nested Flux instance, in that it a) requires
        the initial program of the new instance to be an executable text
        file and b) creates the initial program from a string rather than
        using an executable existing somewhere on the filesystem.

        Use setters to assign additional properties.

        Args:
            script (str): contents of the script to execute, as a string. The
                script should have a shebang (e.g. `#!/bin/sh`) at the top.
            jobname (str): name to use for system.job.name attribute This will
                be the default job name reported by Flux.
            args (iterable of `str`): arguments to pass to `script`
            num_slots (int): number of resource slots to create. Slots are an
                abstraction, and are only used (along with `cores_per_slot`
                and `gpus_per_slot`) to determine the nested instance's
                allocation size and layout.
            cores_per_slot (int): number of cores to allocate per slot
            gpus_per_slot (int): number of GPUs to allocate per slot
            num_nodes (int): distribute allocated resource slots across N
                individual nodes
            broker_opts (iterable of `str`): options to pass to the new Flux
                broker
            conf (dict): optional broker configuration to pass to the
                child instance brokers. If set, `conf` will be set in the
                jobspec 'files' (RFC 37 File Archive) attribute as `conf.json`,
                and broker_opts will be extended to add
                `-c{{tmpdir}}/conf.json`
        """
        if not script.startswith("#!"):
            raise ValueError(f"{jobname} does not appear to start with '#!'")
        args = () if args is None else args
        jobspec = cls.from_nest_command(
            command=["{{tmpdir}}/script", *args],
            num_slots=num_slots,
            cores_per_slot=cores_per_slot,
            gpus_per_slot=gpus_per_slot,
            num_nodes=num_nodes,
            broker_opts=broker_opts,
            exclusive=exclusive,
            conf=conf,
        )
        #  Copy script contents into jobspec
        jobspec.add_file("script", script, perms=0o700, encoding="utf-8")
        jobspec.setattr("system.job.name", jobname)
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
        exclusive=False,
        conf=None,
    ):
        """
        Create a Jobspec describing a nested Flux instance controlled by
        `command`.

        Conceptually, this differs from the `from_batch_command` method
        in that a) the initial program of the nested Flux instance can
        be any executable on the file system, not just a text file and b)
        the executable is not copied at submission time.

        Use setters to assign additional properties.

        Args:
            command (iterable of `str`): initial program for the nested Flux
            instance
            num_slots (int): number of resource slots to create. Slots are
                an abstraction, and are only used (along with `cores_per_slot`
                and `gpus_per_slot`) to determine the nested instance's
                allocation size and layout.
            cores_per_slot (int): number of cores to allocate per slot
            gpus_per_slot (int): number of GPUs to allocate per slot
            num_nodes (int): distribute allocated resource slots across N
                individual nodes
            broker_opts (iterable of `str`): options to pass to the new Flux
                broker
            conf (dict): optional broker configuration to pass to the
                child instance brokers. If set, `conf` will be set in the
                jobspec 'files' (RFC 37 File Archive) attribute as `conf.json`,
                and broker_opts will be extended to add
                `-c{{tmpdir}}/conf.json`
        """
        broker_opts = [] if broker_opts is None else broker_opts
        if conf is not None:
            broker_opts.append("-c{{tmpdir}}/conf.json")
        jobspec = cls.from_command(
            command=["flux", "broker", *broker_opts, *command],
            num_tasks=num_slots,
            cores_per_task=cores_per_slot,
            gpus_per_task=gpus_per_slot,
            num_nodes=num_nodes,
            exclusive=exclusive,
        )
        jobspec.setattr_shell_option("per-resource.type", "node")
        jobspec.setattr_shell_option("mpi", "none")
        if conf is not None:
            jobspec.add_file("conf.json", conf)
        return jobspec
