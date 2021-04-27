"""Integration of Flux's event loop model with Python's own asyncio event loop.

Basic usage: start an asyncio event loop (e.g. with ``asyncio.run()``), call
``start_flux_loop(flux.Flux())``, and then invoke the coroutines provided
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
    loop = asyncio.get_event_loop()
    if hasattr(loop, "_flux_handle"):
        raise RuntimeError("`start_flux_loop` already called")
    loop.call_soon(_run_flux_reactor)
    # pylint: disable=protected-access
    loop._flux_handle = handle


def stop_flux_loop():
    """Stop the Flux-asyncio event loop."""
    loop = asyncio.get_event_loop()
    try:
        # pylint: disable=protected-access
        del loop._flux_handle
    except AttributeError:
        raise RuntimeError("`start_flux_loop` not called")


def _get_loop_flux_handle():
    try:
        # pylint: disable=protected-access
        return asyncio.get_event_loop()._flux_handle
    except AttributeError:
        raise RuntimeError("`start_flux_loop` not called")


def _run_flux_reactor():
    """Run the Flux reactor and schedule another reactor run."""
    try:
        handle = _get_loop_flux_handle()
    except RuntimeError:
        return
    # pylint: disable=no-member
    handle.reactor_run(flags=flux.constants.FLUX_REACTOR_NOWAIT)
    asyncio.get_event_loop().call_soon(_run_flux_reactor)
