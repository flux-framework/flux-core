###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
import json
import os

from flux.future import FutureExt
from flux.idset import IDset
from flux.memoized_property import memoized_property
from flux.resource import ResourceSet


class SchedResourceList:
    """
    Encapsulate response from resource.sched-status query.
    The response will contain 3 Rv1 resource sets:

    :ivar all:         all resources known to scheduler
    :ivar down:        resources currently unavailable (drained or down)
    :ivar allocated:   resources currently allocated to jobs

    From these sets, the "up" and "free" resource sets are
    computed on-demand.

    There is generally no need to instantiate this class directly. Instead,
    instances are returned by fetching the result of a ``resource_list()`` call.
    """

    def __init__(self, resp):
        for state in ["all", "down", "allocated"]:
            rset = ResourceSet(resp.get(state))
            rset.state = state
            setattr(self, f"_{state}", rset)

    def __getattr__(self, attr):
        if attr.startswith("_"):
            raise AttributeError
        try:
            return getattr(self, f"_{attr}")
        except KeyError:
            raise AttributeError(f"Invalid SchedResourceList attr {attr}")

    #  Make class subscriptable, e.g. resources[state]
    def __getitem__(self, item):
        return getattr(self, item)

    def filter(self, include):
        """
        Filter the reported resources in a ResourceList object
        Args:
            include(str, IDset, Hostlist): restrict the current set of
                reported ranks to the given ranks or hosts.
        """
        try:
            include_ranks = IDset(include)
        except ValueError:
            include_ranks = IDset(self["all"].host_ranks(include, ignore_nomatch=True))
        for state in ["all", "down", "allocated"]:
            setattr(self, state, self[state].copy_ranks(include_ranks))

    @memoized_property
    # pylint: disable=invalid-name
    def up(self):
        """All resources which are not down."""
        res = self.all - self.down
        res.state = "up"
        return res

    @memoized_property
    def free(self):
        """All resources which are neither down nor allocated."""
        res = self.up - self.allocated
        res.state = "free"
        return res


class ResourceListRPC(FutureExt):
    def __init__(self, flux_handle, topic, nodeid=0):
        self.topic = topic
        super().__init__(self._init_cb, flux_handle=flux_handle)

    def _init_cb(self, future):
        future.get_flux().rpc(self.topic, nodeid=0).then(self._list_cb)

    def _list_cb(self, future):
        try:
            self.fulfill(future.get())
        except Exception as exc:
            if exc.errno == errno.ENOSYS:
                #  Fall back to sched.resource-status:
                future.get_flux().rpc("sched.resource-status", nodeid=0).then(
                    self._fallback_cb
                )
            else:
                self.fulfill_error(exc.errno, exc.strerror)

    def _fallback_cb(self, future):
        try:
            self.fulfill(json.loads(future.get_str()))
        except OSError as exc:
            self.fulfill_error(exc.errno, exc.strerror)

    def get(self):
        """Return a SchedResourceList corresponding to the request.

        Blocks until the request is fulfilled."""
        return SchedResourceList(super().get())


def resource_list(flux_handle):
    """Send a request for a SchedResourceList object.

    Args:
        flux_handle (flux.Flux): a Flux handle

    Returns:
        ResourceListRPC: a future representing the request.
    """
    topic = os.getenv("FLUX_RESOURCE_LIST_RPC") or "resource.sched-status"
    return ResourceListRPC(flux_handle, topic, nodeid=0)
