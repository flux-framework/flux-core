"""Integration of Flux's event loop model with Python's own asyncio event loop.

Basic usage: start an asyncio event loop (e.g. with `asyncio.run()`), call
`start_flux_loop(flux.Flux())`, and then invoke the coroutines provided
by this package.
"""

import asyncio

import flux.constants


def start_flux_loop(handle):
    """Start the Flux-asyncio event loop.

    This function must be called before any Flux coroutines and
    with an active asyncio event loop.

    :param handle: a `flux.Flux` instance. This handle will be used
        for all Flux coroutines in the current loop.
    :raises RuntimeError: if the Flux-asyncio event loop is active
        (i.e. this function has already been called)
    """
    if hasattr(asyncio.get_event_loop(), "_flux_handle"):
        raise RuntimeError("`start_flux_loop` already called")
    loop = asyncio.get_event_loop()
    file_desc = handle.pollfd()
    # pylint: disable=protected-access
    loop._flux_handle = handle
    loop._flux_pollfd = file_desc
    loop.add_reader(file_desc, _run_flux_reactor)


def stop_flux_loop():
    """Stop the Flux-asyncio event loop."""
    loop = asyncio.get_event_loop()
    try:
        # pylint: disable=protected-access
        loop.remove_reader(loop._flux_pollfd)
        del loop._flux_handle
        del loop._flux_pollfd
    except AttributeError:
        raise RuntimeError("`start_flux_loop` not called")


def _get_loop_flux_handle():
    try:
        # pylint: disable=protected-access
        return asyncio.get_event_loop()._flux_handle
    except AttributeError:
        raise RuntimeError("`start_flux_loop` not called")


def _flux_fatal(handle, msg):
    handle.fatal_error(msg)
    raise RuntimeError(msg)


def _run_flux_reactor():
    """Run the Flux reactor and schedule another reactor run."""
    # pylint: disable=no-member
    handle = _get_loop_flux_handle()
    events = handle.pollevents()
    if events & flux.constants.FLUX_POLLERR:
        _flux_fatal(handle, "Flux pollevents returned POLLERR")
    if events & flux.constants.FLUX_POLLIN:
        returncode = 1  # just set it to something > 0
        while returncode > 0:
            returncode = handle.reactor_run(flags=flux.constants.FLUX_REACTOR_NOWAIT)
        if returncode < 0:
            _flux_fatal(handle, "Reactor run failed")
