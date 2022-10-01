###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from flux.memoized_property import memoized_property
from flux.resource import ResourceSet
from flux.rpc import RPC


class SchedResourceList:
    """
    Encapsulate response from sched.resource-status query.
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


class ResourceListRPC(RPC):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

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
    return ResourceListRPC(flux_handle, "sched.resource-status")
