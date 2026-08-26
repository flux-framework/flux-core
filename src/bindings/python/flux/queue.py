###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import copy
import math

try:
    from dataclasses import dataclass  # novermin
except ModuleNotFoundError:
    from flux.utils.dataclasses import dataclass

from flux.resource import resource_list
from flux.rpc import RPC
from flux.util import dict_merge, parse_fsd

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

    def __init__(self, resources, requires):
        # 'requires' is the queue's effective required-properties array (an
        # RFC 33 virtual queue's is resolved from its parent by the
        # job-manager), or None. A queue with no requires covers the full
        # instance resource set.
        if requires is not None:
            self._resource_list = resources.copy_constraint({"properties": requires})
        else:
            self._resource_list = resources

    def __getattr__(self, attr):
        return getattr(self._resource_list, attr)


def queue_conf_from_config(config):
    """Build the job-manager.queue-list "conf" object from a raw broker config.

    This helper creates a job-manager.queue-list response ``conf`` object
    directly from broker config. For use in testing and as a fallback when
    the job-manager is from an older version of Flux that does not provide
    the queue config directly.

    Returns ``{"queues": [{"name": str, "requires"?: list, "parent"?: str,
    "policy"?: dict}, ...], "policy"?: dict, "default_queue"?: str}``. A
    virtual queue (RFC 33) has no "requires" of its own, so its effective
    "requires" is resolved from its parent. Each queue's "policy" is its
    fully effective policy (the global [policy], the parent's policy for a
    virtual queue, and its own, merged per key). The top-level "policy" is
    the global [policy] (the effective policy for the anonymous queue or a
    job with no queue) and "default_queue" is the default queue name.

    Args:
        config (dict): a broker config, e.g. from the ``config.get`` RPC.

    Raises:
        ValueError: a virtual queue names a parent that is not configured.
    """
    queues = config.get("queues", {})
    global_policy = config.get("policy") or {}
    entries = []
    for name, entry in queues.items():
        conf = {"name": name}
        parent = entry.get("parent")
        # A virtual queue (RFC 33) inherits its parent's requires and layers
        # its own policy over the parent's, so resolve both against the
        # parent entry. An unresolvable parent is fatal (fail closed):
        # falling through would report the vqueue as unconstrained.
        if parent is not None:
            if parent not in queues:
                raise ValueError(
                    f"queue '{name}': parent queue '{parent}' is not configured"
                )
            conf["parent"] = parent
            requires = queues[parent].get("requires")
            base_policy = queues[parent].get("policy")
        else:
            requires = entry.get("requires")
            base_policy = None
        if requires is not None:
            conf["requires"] = requires
        # Effective per-queue policy: the global [policy], then the parent's
        # policy (for a virtual queue), then the queue's own, merged per key
        # from the bottom up (mirrors the job-manager's queue_policy()). Each
        # layer is deep-copied before merging: dict_merge() shares nested
        # dicts from its second argument, so a later merge could otherwise
        # mutate the parent's or global's stored config. An empty result is
        # omitted (empty == absent).
        policy = copy.deepcopy(global_policy)
        if base_policy:
            dict_merge(policy, copy.deepcopy(base_policy))
        dict_merge(policy, copy.deepcopy(entry.get("policy") or {}))
        if policy:
            conf["policy"] = policy
        entries.append(conf)
    result = {"queues": entries}
    if global_policy:
        result["policy"] = global_policy
    try:
        result["default_queue"] = global_policy["jobspec"]["defaults"]["system"][
            "queue"
        ]
    except KeyError:
        # No default queue, return result as is
        pass
    return result


class QueueConf:
    """The job-manager's authoritative queue configuration.

    Wraps a job-manager.queue-list "conf" object (``{"queues": [...],
    "policy"?: {...}, "default_queue"?: str}``) and exposes per-queue
    effective configuration. The job-manager resolves everything, so a
    queue's ``requires`` and ``policy`` are effective values (RFC 33
    virtual-queue inheritance and the global policy are already merged in).

    Fetch from a live instance with :func:`queue_config_fetch`. Build from a
    raw broker config with :meth:`from_config` (the ``--config-file`` /
    stdin / test path). The empty (anonymous-queue) case is a QueueConf with
    no entries.
    """

    def __init__(self, conf):
        self._entries = {entry["name"]: entry for entry in conf.get("queues", [])}
        # The global policy is the effective policy for the anonymous queue
        # or a job with no queue (a named queue's effective policy is in its
        # own entry).
        self._global_policy = conf.get("policy") or {}
        self._default_queue = conf.get("default_queue", "")

    @classmethod
    def from_config(cls, config):
        """Return a QueueConf built from a raw broker config (the config-file
        test path). See :func:`queue_conf_from_config`.
        """
        return cls(queue_conf_from_config(config))

    def __contains__(self, name):
        return name in self._entries

    def __iter__(self):
        return iter(self._entries)

    def __len__(self):
        # Number of named queues (0 in the anonymous-queue case), so a
        # QueueConf is falsy when no named queues are configured.
        return len(self._entries)

    @property
    def entries(self):
        """The raw name -> conf-entry dict.

        A low-level escape hatch for bulk/iteration consumers whose access
        pattern does not fit the per-queue accessors (requires/parent) --
        e.g. flux-resource's dict-oriented rendering. Prefer the accessors
        for per-queue lookups.
        """
        return self._entries

    def requires(self, name):
        """The queue's effective required-properties list, or None.

        Already vqueue-resolved by the job-manager (a virtual queue reports
        its parent's requires). ``name`` may be None or an unconfigured queue
        (the anonymous-queue case) -> None.
        """
        return (self._entries.get(name) or {}).get("requires")

    def parent(self, name):
        """The queue's resolved parent name (RFC 33 virtual queue), or "".

        The job-manager's authoritative view of the resolved parent.
        """
        return (self._entries.get(name) or {}).get("parent", "")

    def policy(self, name=None):
        """The queue's effective policy dict (empty if none).

        The job-manager has already merged the global policy, RFC 33
        virtual-queue inheritance, and the queue's own policy, so this is a
        direct lookup. ``name`` None or an unconfigured queue (the
        anonymous-queue case) -> the global policy.
        """
        entry = self._entries.get(name)
        if entry is None:
            return self._global_policy
        return entry.get("policy") or {}

    def defaults(self, name=None):
        """The queue's effective job defaults
        (``policy.jobspec.defaults.system``), an empty dict if none.
        """
        try:
            return self.policy(name)["jobspec"]["defaults"]["system"]
        except KeyError:
            return {}

    @property
    def default_queue(self):
        """The configured default queue name, or "" if none."""
        return self._default_queue


class QueueConfRPC(RPC):
    """A pending queue configuration fetch from a Flux instance.

    Sends the ``job-manager.queue-list`` RPC on construction (so it can
    overlap with other work) and returns a :obj:`QueueConf` from
    :meth:`get`. An older job-manager omits the "conf" object, so
    :meth:`get` then falls back to deriving the config from the broker
    config via a synchronous ``config.get`` RPC.
    """

    def __init__(self, flux_handle):
        super().__init__(flux_handle, "job-manager.queue-list")

    def get(self):
        """Return the :obj:`QueueConf`. Blocks until the request completes."""
        conf = super().get().get("conf")
        if conf is None:
            #  Older job-manager without "conf": fall back to config.
            conf = queue_conf_from_config(self._handle.rpc("config.get").get())
        return QueueConf(conf)


def queue_config_fetch(handle):
    """Send a request for the instance's queue configuration.

    Args:
        handle (:obj:`flux.Flux`): a Flux handle

    Returns:
        QueueConfRPC: a pending fetch; :meth:`~QueueConfRPC.get` returns a
        :obj:`QueueConf`. The request is sent immediately, so the fetch can
        overlap with other RPCs before :meth:`~QueueConfRPC.get` is called.
    """
    return QueueConfRPC(handle)


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
        queue_conf,
        resources,
        enabled,
        started,
        default,
        blocked=None,
    ):
        # 'queue_conf' is a QueueConf (the job-manager's authoritative queue
        # configuration). It provides this queue's effective requires, its
        # resolved parent, and its effective policy (the global policy and
        # RFC 33 virtual-queue inheritance already merged in). 'name' is
        # None for the anonymous queue.
        self.name = name or ""
        self.is_default = default
        self.enabled = enabled
        self.started = started
        # RFC 33 virtual queues: parent is the job-manager's resolved parent.
        self.parent = queue_conf.parent(name)
        self.blocked = blocked
        self.resources = QueueResources(resources, queue_conf.requires(name))
        self._policy = queue_conf.policy(name)
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
        limit = "max" if maximum else "min"
        try:
            val = self._policy["limits"]["job-size"][limit][key]
        except KeyError:
            return math.inf if maximum else 0
        return math.inf if val < 0 else val

    def _policy_default(self, key, default="inf"):
        try:
            return self._policy["jobspec"]["defaults"]["system"][key]
        except KeyError:
            return default

    def _policy_system_limit(self, key, default="inf"):
        try:
            return self._policy["limits"][key]
        except KeyError:
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

        # Gather the resource list and the queue configuration in parallel:
        resources, conf = map(
            lambda x: x.get(),
            [resource_list(handle), queue_config_fetch(handle)],
        )

        self.default_queue = conf.default_queue
        selected = self.__select_queues(conf, queues)
        status = self.__fetch_queue_status(handle, selected)

        # If there's a single anonymous queue, self.__queue will not be None
        self.__queue = None

        if not selected:
            # single anonymous queue (no named queues configured):
            self.__queue = QueueInfo(
                None,
                conf,
                resources,
                status["enable"],
                status["start"],
                True,
                status.get("blocked"),
            )
            self.__queues = {"": self.__queue}
        else:
            # multiple configured queues, keyed by name
            self.__queues = {
                name: QueueInfo(
                    name,
                    conf,
                    resources,
                    status[name]["enable"],
                    status[name]["start"],
                    name == self.default_queue,
                    status[name].get("blocked"),
                )
                for name in selected
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
    def __select_queues(conf, queues):
        """
        Return the list of queue names in ``conf`` (a QueueConf) selected by
        ``queues``. If ``queues`` is None or an empty list or set, return all
        configured queue names, which may be empty if there are none.
        """
        if not queues:
            return list(conf)

        # Otherwise, return the selected queues only:
        result = []
        for queue in queues:
            if queue not in conf:
                raise ValueError(f"No such queue: {queue}")
            result.append(queue)
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
