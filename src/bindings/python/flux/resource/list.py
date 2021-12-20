###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from flux.resource import ResourceSet
from flux.rpc import RPC
from flux.memoized_property import memoized_property


class SchedResourceList:
    """
    Encapsulate response from sched.resource-status query.
    The response will contain 3 Rv1 resource sets:
        "all"       - all resources known to scheduler
        "down"      - resources currently unavailable (drained or down)
        "allocated" - resources currently allocated to jobs

    From these sets, the "up" and "free" resource sets are
    computed on-demand.

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
        res = self.all - self.down
        res.state = "up"
        return res

    @memoized_property
    def free(self):
        res = self.up - self.allocated
        res.state = "free"
        return res


class ResourceListRPC(RPC):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def get(self):
        return SchedResourceList(super().get())


def resource_list(flux_handle):
    return ResourceListRPC(flux_handle, "sched.resource-status")
