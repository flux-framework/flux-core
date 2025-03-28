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

    def __init__(self, name, resources, config):
        if (
            name
            and "queues" in config
            and name in config["queues"]
            and "requires" in config["queues"][name]
        ):
            self._resource_list = resources.copy_constraint(
                {"properties": config["queues"][name]["requires"]}
            )
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
        started (bool): True if this queue is started
        resources (:obj:`QueueResources`): resources currently in this queue
        defaults (:obj:`QueueDefaults`): defaults that apply to this queue
        limits (:obj:`QueueLimits`): policy limits that apply to this queue
    """

    def __init__(self, name, config, resources, enabled, started, default):
        self.name = name or ""
        self.config = config
        self.is_default = default
        self.enabled = enabled
        self.started = started
        self.resources = QueueResources(name, resources, config)
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

    def _size_limit(self, key, maximum=True):
        limit = maximum and "max" or "min"
        try:
            val = self.config["queues"][self.name]["policy"]["limits"]["job-size"][
                limit
            ][key]
        except KeyError:
            try:
                val = self.config["policy"]["limits"]["job-size"][limit][key]
            except KeyError:
                val = math.inf if maximum else 0
            if val < 0:
                val = math.inf
        return val

    def _policy_default(self, key, default="inf"):
        try:
            result = self.config["queues"][self.name]["policy"]["jobspec"]["defaults"][
                "system"
            ][key]
        except KeyError:
            try:
                result = self.config["policy"]["jobspec"]["defaults"]["system"][key]
            except KeyError:
                result = default
        return result

    def _policy_system_limit(self, key, default="inf"):
        try:
            result = self.config["queues"][self.name]["policy"]["limits"][key]
        except KeyError:
            try:
                result = self.config["policy"]["limits"][key]
            except KeyError:
                result = default
        return result


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
                None, config, resources, status["enable"], status["start"], True
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
