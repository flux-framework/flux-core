###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import math

try:
    from dataclasses import dataclass  # novermin
except ModuleNotFoundError:
    from flux.utils.dataclasses import dataclass

from flux.resource import resource_list
from flux.util import parse_fsd

"""Simple access to information about Flux queues from Python.

This module allows simple access to Flux queue configuration, state,
and resource statistics from Python.

The main interface for obtaining information is the :obj:`QueueList`
class. The class is initiated via

 >>> handle = flux.Flux()
 >>> qlist = QueueList(handle)

By default, Flux runs with a single anonymous queue, but multiple named
queues may specified in configuration.

A :obj:`QueueList` object may be iterated to get the list of queues,
where each entry is a :obj:`QueueInfo` object. For example, the list
of configured queue names could be emitted via:

 >>> for queue in qlist:
 >>>    print(queue.name)

Alternately, a :obj:`QueueList` object may be indexed by queue name to
return a specific queue by name:

 >>> batchq = qlist["batch"]

If there is a single anonymous queue, the queue name is the empty string,
so this queue may be indexed using an empty string as the key:

 >>> queue = qlist[""]

or, of course, as the first and only element of the list of queues:

 >>> queue = list(qlist)[0]

Queue information is then obtained by attribute on the :obj:`QueueInfo`
object for each queue, below is a summary of available attributes. These
attributes are also documented for each corresponding class in this module::

  queue.name (str) (empty string if using a single anonymous queue)
  queue.is_default (bool)
  queue.enabled (bool)
  queue.started (bool)
  queue.parent (str) (empty string unless this is an RFC 33 virtual queue)
  queue.defaults.duration (float)
  queue.limits.min.{nnodes,ncores,ngpus} (int)
  queue.limits.max.{nnodes,ncores,ngpus} (int)
  queue.limits.duration (float)
  queue.resources.[up,down,allocated,free,all] (ResourceSet)

"""


@dataclass
class QueueResourceCounts:
    """
    Class containing counts of basic resources, used by :obj:`QueueLimits`
    and :obj:`QueueDefaults`.

    Attributes:
        nnodes (float): Count of nodes
        ncores (float): Count of cores
        ngpus (float): Count of gpus
    """

    nnodes: float
    ncores: float
    ngpus: float


@dataclass
class QueueLimits:
    """
    Class representing queue limits.

    Attributes:
        min (:obj:`QueueResourceCounts`): Configured resource minimums.
        max (:obj:`QueueResourceCounts`): Configured resource maximums.
            (If no maximum, individual resource count will be ``inf``)
        duration (float): duration limit in seconds.
        timelimit (float): synonym for duration.
    """

    min: QueueResourceCounts
    max: QueueResourceCounts
    duration: float

    @property
    def timelimit(self):
        """
        timelimit is a alternate field name for duration for use in
        ``flux queue list``
        """
        return self.duration


@dataclass
class QueueDefaults:
    """
    Class representing common queue defaults.

    Attributes:
        duration (float): Default job duration in seconds.
        timelimit (float): Synonym for duration.
    """

    duration: float

    @property
    def timelimit(self):
        """
        timelimit is a alternate field name for duration for use in
        ``flux queue list``
        """
        return self.duration


class QueueResources:
    """
    Container for resources assigned to an individual queue.

    Attrs:
        all (:obj:`flux.resource.ResourceSet`): all configured resources
        down (:obj:`flux.resource.ResourceSet`): down resources
        up (:obj:`flux.resource.ResourceSet`): resources that are not down
        allocated (:obj:`flux.resource.ResourceSet`): resources allocated to
            jobs
        free (:obj:`flux.resource.ResourceSet`): resources not down or
            allocated to jobs
    """

    def __init__(self, name, resources, config, parent=""):
        # A virtual queue (RFC 33) has no "requires" of its own (enforced
        # at config validation), so it takes its parent's resource slice
        # instead of the full instance resource set. 'parent' comes from
        # the queue-status RPC while 'config' comes from a separate
        # config.get RPC, so a config reload can race the two: a parent
        # that is no longer in config is an error (fail closed), since
        # falling through would silently report the vqueue as covering
        # the full instance resource set.
        requires = None
        queues = config.get("queues", {})
        if name and name in queues:
            requires = queues[name].get("requires")
        if requires is None and parent:
            if parent not in queues:
                raise ValueError(
                    f"queue '{name}': parent queue '{parent}' is not configured"
                )
            requires = queues[parent].get("requires")
        if requires is not None:
            self._resource_list = resources.copy_constraint({"properties": requires})
        else:
            self._resource_list = resources

    def __getattr__(self, attr):
        return getattr(self._resource_list, attr)


class QueueInfo:
    """
    Information for a single queue.

    Attrs:
        name (str): The queue name (empty string for anonymous queue)
        is_default (bool): True if this is the default queue
        enabled (bool): True if this queue is enabled
        started (bool): True if this queue is started (effective state,
            i.e. own AND parent for an RFC 33 virtual queue)
        blocked (str): Reason keyword (RFC 33) if this queue's own started
            bit is set but it is effectively stopped by an external
            condition: "scheduler" (scheduler offline) or "parent" (a
            virtual queue's parent is stopped). None otherwise. An
            unrecognized keyword is treated as a generic blocked condition.
        parent (str): Name of the parent queue if this is an RFC 33
            virtual queue, otherwise an empty string
        resources (:obj:`QueueResources`): resources currently in this queue
        defaults (:obj:`QueueDefaults`): defaults that apply to this queue
        limits (:obj:`QueueLimits`): policy limits that apply to this queue
    """

    def __init__(
        self,
        name,
        config,
        resources,
        enabled,
        started,
        default,
        parent="",
        blocked=None,
    ):
        self.name = name or ""
        self.config = config
        self.is_default = default
        self.enabled = enabled
        self.started = started
        # RFC 33 virtual queues: parent is sourced from the queue-status
        # RPC response rather than config, since that is the job-manager's
        # authoritative view of the resolved parent (config is also in
        # hand here, but a single source avoids the two ever disagreeing).
        self.parent = parent or ""
        self.blocked = blocked
        self.resources = QueueResources(name, resources, config, self.parent)
        self.defaults = QueueDefaults(
            duration=parse_fsd(self._policy_default("duration"))
        )
        self.limits = QueueLimits(
            min=QueueResourceCounts(
                nnodes=self._size_limit("nnodes", False),
                ncores=self._size_limit("ncores", False),
                ngpus=self._size_limit("ngpus", False),
            ),
            max=QueueResourceCounts(
                nnodes=self._size_limit("nnodes"),
                ncores=self._size_limit("ncores"),
                ngpus=self._size_limit("ngpus"),
            ),
            duration=parse_fsd(self._policy_system_limit("duration")),
        )

    def _config_entries(self):
        """
        Yield config entries to search for a policy/limit/default key, in
        RFC 33 virtual queue inheritance order: this queue's own config
        entry, its parent's config entry (if this is a virtual queue),
        then the global config. Each key is looked up independently in
        this order (per-key inheritance), so a vqueue setting only one
        key still inherits its parent's other keys. A parent missing
        from config (a config reload racing the queue-status RPC, see
        QueueResources) is an error: silently skipping the parent layer
        would report the wrong effective limits and defaults.
        """
        queues = self.config.get("queues", {})
        if self.name in queues:
            yield queues[self.name]
        if self.parent:
            if self.parent not in queues:
                raise ValueError(
                    f"queue '{self.name}': parent queue '{self.parent}'"
                    " is not configured"
                )
            yield queues[self.parent]
        yield self.config

    def _size_limit(self, key, maximum=True):
        limit = maximum and "max" or "min"
        for entry in self._config_entries():
            try:
                val = entry["policy"]["limits"]["job-size"][limit][key]
            except KeyError:
                continue
            if val < 0:
                val = math.inf
            return val
        return math.inf if maximum else 0

    def _policy_default(self, key, default="inf"):
        for entry in self._config_entries():
            try:
                return entry["policy"]["jobspec"]["defaults"]["system"][key]
            except KeyError:
                continue
        return default

    def _policy_system_limit(self, key, default="inf"):
        for entry in self._config_entries():
            try:
                return entry["policy"]["limits"][key]
            except KeyError:
                continue
        return default


class QueueList:
    """Gather information about currently configured Flux queues.

    Args:
        handle (:obj:`flux.Flux`): handle to Flux
        queues (list): Optional list of queue names to target. If None
            or an empty list, then information for all configured queues
            will be targeted.
    """

    def __init__(self, handle, queues=None):

        # Gather resource list and current full config in parallel:
        resources, config = map(
            lambda x: x.get(),
            [resource_list(handle), handle.rpc("config.get")],
        )

        self.default_queue = self.__default_queue(config)
        queue_config = self.__queue_config(config, queues)
        status = self.__fetch_queue_status(handle, queue_config.keys())

        # If there's a single anonymous queue, self.__queue will not be None
        self.__queue = None

        if not queue_config:
            # single anonymous queue:
            self.__queue = QueueInfo(
                None,
                config,
                resources,
                status["enable"],
                status["start"],
                True,
                status.get("parent", ""),
                status.get("blocked"),
            )
            self.__queues = {"": self.__queue}
        else:
            # multiple configured queues, keyed by name
            self.__queues = {
                x: QueueInfo(
                    x,
                    config,
                    resources,
                    status[x]["enable"],
                    status[x]["start"],
                    x == self.default_queue,
                    status[x].get("parent", ""),
                    status[x].get("blocked"),
                )
                for x in queue_config.keys()
            }

    def __getattr__(self, attr):
        # Allow queue to be obtained with qlist.<name>
        return self.__queues[attr]

    def __getitem__(self, item):
        # allow anonymous queue to be referred to with None or ""
        if item is None:
            return self.__queue
        return self.__queues[item]

    def __iter__(self):
        if self.__queues:
            return iter(self.__queues.values())
        return iter([None])

    @staticmethod
    def __default_queue(config):
        """
        Return configured default queue name or an empty string if
        there's a single anonymous queue
        """
        try:
            return config["policy"]["jobspec"]["defaults"]["system"]["queue"]
        except KeyError:
            return ""

    @staticmethod
    def __queue_config(config, queues):
        """
        Return a subset of the queue config in ``config`` given ``queues``.
        If ``queues`` is None or an empty list or set, return the whole config,
        which may be empty if there are no configured queues.
        """
        if not queues:
            return config.get("queues", {})

        # Otherwise, return subset of queue config only for selected queues:
        result = {}
        for queue in queues:
            try:
                result[queue] = config["queues"][queue]
            except KeyError:
                raise ValueError(f"No such queue: {queue}")
        return result

    @staticmethod
    def __fetch_queue_status(handle, queues):
        """
        Fetch the queue enable/start status for all queues, or the single
        anonymous queue if queues is empty.
        """
        if not queues:
            # Single anonymous queue:
            return handle.rpc("job-manager.queue-status", {}).get()

        # else, separate rpc per queue
        rpcs = {
            queue: handle.rpc("job-manager.queue-status", {"name": queue})
            for queue in queues
        }
        return {queue: rpcs[queue].get() for queue in rpcs}
