###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import asyncio

import flux
import flux.core.watchers

from flux.asyncio.selector import FluxSelector

# The loop *must* have the same handle as the submit or else the loop will
# run forever.
HANDLE = flux.Flux()


async def submit(jobspec, flux_handle=None):
    """Submit a Flux jobspec and return a job ID.

    Example usage
    =============
    # ensure the script is running in an active flux instance
    import asyncio
    from flux.asyncio import loop
    import flux.job

    fluxsleep = flux.job.JobspecV1.from_command(['sleep', '2'])
    fluxecho = flux.job.JobspecV1.from_command(['echo', 'pancakes'])

    tasks = [
       loop.create_task(asyncio.sleep(5)),
       loop.create_task(flux.asyncio.submit(fluxecho)),
       loop.create_task(flux.asyncio.submit(fluxsleep)),
    ]

    asyncio.set_event_loop(loop)
    results = loop.run_until_complete(asyncio.gather(*tasks))
    # [JobID(456004999315456), JobID(456004999315457)]
    """
    handle = flux_handle or HANDLE
    uid = await flux.job.submit_async(handle, jobspec)
    return uid


class FluxEventLoop(asyncio.SelectorEventLoop):
    """An asyncio loop that handles running Flux."""

    def __init__(self, flux_handle=None):
        # This can be provided by a user that knows what they are doing.
        # The handle to the submit job and loop must be the same.
        if not flux_handle:
            flux_handle = HANDLE
        selector = FluxSelector(flux_handle)

        # Reverse reference is needed for watchers
        selector.loop = self
        super().__init__(selector)

    @property
    def selector(self):
        return self._selector


# This loop needs to be accessible from all places!
loop = FluxEventLoop()  # pylint: disable=invalid-name
