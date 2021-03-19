"""Flux-asyncio integration for ``flux.job`` functions."""

import asyncio

import flux.job
from flux.asyncio import _get_loop_flux_handle


async def _asyncio_flux_wrapper(flux_func, getter, args, kwargs):
    """Convert a flux async function into a coroutine.

    :param flux_func: the function to wrap. Should return a Flux future
    :param getter: the function to get the result of the Flux future. If None,
        use future.get()
    :param args: args for flux_func
    :param kwargs: kwargs for flux_func
    """
    handle = _get_loop_flux_handle()
    asyncio_fut = asyncio.get_event_loop().create_future()
    if getter is None:
        callback = lambda flux_fut: asyncio_fut.set_result(flux_fut.get())
    else:
        callback = lambda flux_fut: asyncio_fut.set_result(getter(flux_fut))
    flux_func(handle, *args, **kwargs).then(callback)
    return await asyncio_fut


async def submit(*args, **kwargs):
    """Submit a new job and return the job ID.

    Has the same signature as ``flux.job.submit``, minus the first argument.

    :raises RuntimeError: if the Flux-asyncio event loop is not active.
    """
    return await _asyncio_flux_wrapper(
        flux.job.submit_async, flux.job.submit_get_id, args, kwargs
    )


async def cancel(*args, **kwargs):
    """Cancel a running or pending job.

    Has the same signature as ``flux.job.cancel``, minus the first argument.

    :raises RuntimeError: if the Flux-asyncio event loop is not active.
    """
    return await _asyncio_flux_wrapper(flux.job.cancel_async, None, args, kwargs)


async def kill(*args, **kwargs):
    """Send a signal to a running job.

    Has the same signature as ``flux.job.kill``, minus the first argument.

    :raises RuntimeError: if the Flux-asyncio event loop is not active.
    """
    return await _asyncio_flux_wrapper(flux.job.kill_async, None, args, kwargs)


async def wait(*args, **kwargs):
    """Wait for a job to complete and return a ``flux.job.JobWaitResult``.

    A lighter-weight function than ``result``, this function requires that
    the specified job be waitable (``flux.job.submit(..., waitable=True)``).

    Has the same signature as ``flux.job.wait``, minus the first argument.

    :raises RuntimeError: if the Flux-asyncio event loop is not active.
    """
    return await _asyncio_flux_wrapper(
        flux.job.wait_async, flux.job.wait_get_status, args, kwargs
    )


async def result(*args, **kwargs):
    """Wait for a job to complete and return a ``flux.job.JobInfo``.

    A more heavyweight function than ``wait``, this function does not require that
    the specified job be waitable (``flux.job.submit(..., waitable=some_boolean)``).

    Has the same signature as ``flux.job.result``, minus the first argument.

    :raises RuntimeError: if the Flux-asyncio event loop is not active.
    """
    return await _asyncio_flux_wrapper(
        flux.job.result_async, flux.job.JobResultFuture.get_info, args, kwargs
    )


async def event_watch(*args, **kwargs):
    """Asynchronous generator that yields `flux.job.EventLogEvent` instances.

    Accepts the same arguments as ``flux.job.event_watch``, minus the first argument.

    :raises RuntimeError: if the Flux-asyncio event loop is not active.
    """
    handle = _get_loop_flux_handle()
    queue = asyncio.Queue()
    flux.job.event_watch_async(handle, *args, **kwargs).then(
        lambda flux_fut: queue.put_nowait(flux_fut.get_event())
    )
    while True:
        event = await queue.get()
        if event is None:
            return
        yield event
