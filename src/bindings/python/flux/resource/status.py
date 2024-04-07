###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from typing import NamedTuple

from flux.idset import IDset
from flux.resource import ResourceSet, resource_list
from flux.rpc import RPC


class DrainInfo(NamedTuple):
    """
    Drained resource information tuple.
    Attributes:
        timestamp (float): timestamp at which resource was drained
        reason (str): message recorded when resources were drained
        ranks (IDset): idset of drain ranks with this reason and timestamp

    """

    ranks: IDset
    timestamp: float
    reason: str


class ResourceStatus:
    """
    Container for combined information from resource module and scheduler.

    Attributes:
        nodelist (Hostlist): rank ordered set of hostnames
        all (IDset): idset of all known ranks
        avail (IDset): idset of ranks not excluded or drained
        offline (IDset): idset of ranks currently offline
        online (IDset): idset of ranks currently online
        exclude (IDset): idset of ranks excluded by configuration
        allocated (IDset): idset of ranks with one or more jobs
        drained (IDset): idset of ranks drained and not allocated
        draining (IDset): idset of ranks drained and allocated
        drain_info (list): list of DrainInfo object for drain ranks
    """

    def __init__(self, rstatus=None, allocated_ranks=None):
        # Allow "empty" ResourceStatus object to be created:
        if rstatus is None:
            rstatus = dict(R=None, offline="", online="", exclude="", drain={})
        if allocated_ranks is None:
            allocated_ranks = IDset()

        self.rstatus = rstatus
        self.rset = ResourceSet(rstatus["R"])
        self.nodelist = self.rset.nodelist
        self.allocated_ranks = allocated_ranks

        self._recalculate()

    def filter(self, include):
        """
        Filter the reported resources in a ResourceStatus object
        Args:
            include (str, IDset, Hostlist): restrict the current set of
             reported ranks to the given ranks or hosts.
        """
        try:
            include_ranks = IDset(include)
        except ValueError:
            include_ranks = self.nodelist.index(include, ignore_nomatch=True)
        self._recalculate(include_ranks)

    def _recalculate(self, include_ranks=None):
        """
        Recalculate derived idsets and drain_info, only including ranks
        in the IDset 'include_ranks' if given.
        Args:
            include_ranks (IDset): restrict the current set of reported ranks.
        """
        # get idset of all ranks:
        self.all = self.rset.ranks

        # offline/online
        self.offline = IDset(self.rstatus["offline"])
        self.online = IDset(self.rstatus["online"])

        # excluded: excluded by configuration
        self.exclude = IDset(self.rstatus["exclude"])

        # allocated: online and allocated by scheduler
        self.allocated = self.allocated_ranks

        # If include_ranks was provided, filter all idsets to only those
        # that intersect the provided idset
        if include_ranks is not None:
            for name in ("all", "offline", "online", "exclude", "allocated"):
                result = getattr(self, name).intersect(include_ranks)
                setattr(self, name, result)

        # drained: free+drain
        self.drained = IDset()
        # draining: allocated+drain
        self.draining = IDset()

        # drain_info: ranks, timestamp, reason tuples for all drained resources
        self.drain_info = []
        self._drain_lookup = {}
        for drain_ranks, entry in self.rstatus["drain"].items():
            ranks = IDset(drain_ranks)
            if include_ranks is not None:
                ranks = ranks.intersect(include_ranks)
            self.drained += ranks
            info = DrainInfo(ranks, entry["timestamp"], entry["reason"])
            self.drain_info.append(info)
            for rank in ranks:
                self._drain_lookup[rank] = info

        # create the set of draining ranks as the intersection of
        #  drained and allocated
        self.draining = self.drained & self.allocated
        self.drained -= self.draining

        # available: all ranks not excluded or drained/draining
        self.avail = self.all - self.get_idset("exclude", "drained", "draining")

    def __getitem__(self, state):
        """
        Allow a ResourceStatus object to be subscriptable for convenience,
        e.g. self.drained == self["drained"]
        """
        return getattr(self, state)

    def get_idset(self, *args):
        """
        Return an idset of ranks that are the union of all states in args
        """
        ids = IDset()
        for state in args:
            ids.add(self[state])
        return ids

    def get_drain_info(self, rank):
        """
        Find and return the DrainInfo object for rank ``rank``
        """
        if rank not in self.all:
            raise ValueError("invalid rank {rank}")
        return self._drain_lookup.get(rank)


class ResourceStatusRPC:
    """
    A ResourceStatusRPC encapsulates a query to both the resource module
    and scheduler and returns a ResourceStatus object.
    """

    def __init__(self, handle):
        self.rpcs = [
            RPC(handle, "resource.status", nodeid=0, flags=0),
            resource_list(handle),
        ]
        self.rlist = None
        self.rstatus = None
        self.allocated_ranks = None

    def get_status(self):
        if self.rstatus is None:
            self.rstatus = self.rpcs[0].get()
        return self.rstatus

    def get_allocated_ranks(self):
        if self.allocated_ranks is None:
            try:
                self.rlist = self.rpcs[1].get()
                self.allocated_ranks = self.rlist.allocated.ranks
            except EnvironmentError:
                self.allocated_ranks = IDset()
        return self.allocated_ranks

    def get(self):
        """
        Return a ResourceStatus object corresponding to the request
        Blocks until all RPCs are fulfilled
        """
        return ResourceStatus(self.get_status(), self.get_allocated_ranks())


def resource_status(flux_handle):
    """
    Initiate RPCs to scheduler and resource module and return a ResourceStatus
    object holding the result.

    Args:
        flux_handle (flux.Flux): a Flux handle

    Returns:
        ResourceStatusRPC: A future representing the request. Call .get()
         to get the ResourceStatus result.
    """
    return ResourceStatusRPC(flux_handle)
