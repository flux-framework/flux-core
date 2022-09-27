.. _python_job_submission:

Flux Job Submission and Monitoring
==================================

The Flux Python bindings provide synchronous and asynchronous
functions for:

* Submitting jobs with arbitrary attributes and resources
* Waiting for jobs to complete
* Cancelling jobs
* Viewing job info and events


Job submission
--------------

Job submission is performed by creating a ``flux.job.Jobspec`` object,
populating it with attributes, and then passing it to one of the submission
functions, e.g. :ref:`flux.job.submit <python_flux_job_submit_func>`. Jobspec
objects define everything
about a job, including the job's resources, executable, working directory,
environment, and stdio streams.

Basic Jobspec creation is generally done with the
``JobspecV1.from_command`` class method and its
variants ``from_batch_command`` and ``from_nest_command``, which are helper
methods replicating the jobspecs created by the
``flux mini`` command-line utilities.


.. autoclass:: flux.job.Jobspec
	:members:

.. autoclass:: flux.job.JobspecV1
	:show-inheritance:
	:members:



Job manipulation
----------------

After a job has been submitted, it will be assigned an ID. That ID
can then be used for getting information about the job or
for manipulating it---see the synchronous and asynchronous sections below.

To translate job ID representations, use the ``flux.job.JobID`` class:

.. autoclass:: flux.job.JobID
	:members:


Synchronous interface
---------------------


The simplest way to interact with Flux is with the syncronous functions listed
below.
However, these functions introduce a lot of overhead (e.g. any function that
waits for a job to reach a certain state, such as ``result``
may block for an indeterminate amount of time) and may not be suitable for
interacting with large numbers of jobs in time-sensitive applications.

To spend less time blocking in the Flux reactor, consider using one
of the asynchronous interfaces.


.. _python_flux_job_submit_func:
.. autofunction:: flux.job.submit

.. autofunction:: flux.job.event_watch

.. autofunction:: flux.job.event_wait

.. autofunction:: flux.job.kill

.. autofunction:: flux.job.cancel

.. autofunction:: flux.job.result

.. autofunction:: flux.job.wait


``result`` vs ``wait``
~~~~~~~~~~~~~~~~~~~~~~

Both ``flux.job.result`` and ``flux.job.wait`` return when a job has completed.
However, ``wait`` only works on jobs which have been submitted
with the ``waitable`` flag, and the ability to set that flag is
restricted to instance owners.


Asynchronous interfaces
-----------------------

There are two primary asynchronous interfaces to job manipulations, and a third
that uses Python asyncio to submit jobs. The first is an event-loop interface,
which is closer to the native C interface, and consists of functions like 
``flux.job.submit_async`` and ``flux.job.result_async`` (note the
functions are the same as in the synchronous interface, only with an "_async"
suffix). The second is an interface which is almost identical to the
`concurrent.futures <https://docs.python.org/3/library/concurrent.futures.html>`_
interface in Python's standard library, and it consists of the
``flux.job.FluxExecutor`` class. Both interfaces deal in callbacks and
futures, the difference being that the ``FluxExecutor`` is designed so that
all futures fulfill in the background, and there is no need for user code
to enter the Flux event loop, while the event-loop-based interface
requires the user to call into the Flux event loop in order for futures
to fulfill and for callbacks to trigger. For the third interface (using asyncio)
we are still using Flux' implementation of futures, but we create some of
the backend watchers (e.g., a file descriptor, timer, or signal watcher)
within the context of a customized asyncio event loop. This means that we are
able to have Flux run alongside a more traditional (and native) Python 
asyncronous interface.

Our general recommendation is that you use the ``FluxExecutor`` interface
unless you are familiar with event-loop based programming.


The ``FluxExecutor`` interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Basic ``FluxExecutor`` usage consists of creating an executor,
submitting jobspecs to it, and then attaching callbacks to those
futures or waiting for them to complete. Executors must have
``.shutdown()`` called when they are no longer to be used. However,
they also support the context-manager protocol (i.e. ``with executor ...:``)
which will call ``shutdown`` upon leaving the ``with`` block.

Example usage:

.. code:: python

	import concurrent.futures
	import flux.job

	jobspec = flux.job.JobspecV1.from_command(["/bin/true"])
	with flux.job.FluxExecutor() as executor:
		futs = [executor.submit(jobspec) for _ in range(5)]
		for f in concurrent.futures.as_completed(futs):
			print(f.result())


.. autoclass:: flux.job.FluxExecutor
	:members:

Futures should not be created directly by user code. Since
the futures are subclasses of ``concurrent.executors.Future``,
you can invoke ``concurrent.futures`` functions like ``wait`` or
``as_completed`` on them.

.. autoclass:: flux.job.FluxExecutorFuture
	:members:


Asynchronous event-loop interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

General usage consists of initiating some events,
then entering the Flux reactor and writing all code
thereafter as callbacks.

For instance, the below example submits five jobs,
then enters the reactor and fires off callbacks
as futures complete.

.. code:: python

	import flux
	import flux.job

	def submit_cb(fut, flux_handle):
		# when this callback fires, the jobid will be ready
		jobid = fut.get_id()
		# Create a future representing the result of the job
		result_fut = flux.job.result_async(flux_handle, jobid)
		# attach a callback to fire when the job finishes
		result_fut.then(result_cb)

	def result_cb(fut):
		print(fut.get_info())

	flux_handle = flux.Flux()
	jobspec = flux.job.JobspecV1.from_command(["/bin/true"])
	for _ in range(5):
		# submit 5 futures and attach callbacks to each one
		submit_future = flux.job.submit_async(f, jobspec)
		submit_future.then(submit_cb, flux_handle)
	# enter the flux event loop (the 'reactor') to trigger the callbacks
	# once the futures complete
	flux_handle.reactor_run()


.. autofunction:: flux.job.submit_async

.. autofunction:: flux.job.event_watch_async

.. autofunction:: flux.job.cancel_async

.. autofunction:: flux.job.kill_async

.. autofunction:: flux.job.result_async

.. autofunction:: flux.job.wait_async


The ``FluxEventLoop`` (asyncio) interface
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you are familiar with `asyncio <https://docs.python.org/3/library/asyncio.html>`_
Flux has support for using these native Python event loops to submit jobs. As an example,
here we use the ``FluxEventLoop`` to submit a job, along with running an ``asyncio.sleep`` job.


.. code:: python

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


For the above, you get back the job ID. Note that we also use a common event loop 
and handle that ``flux.asyncio`` defines. 
You are, however, free to use the ``FluxEventLoop`` (loop already created in the
example above) class to define your own, using your own Flux handle, which is
accepted as the only init argument to the loop class.

.. autofunction:: flux.asyncio.submit

.. autofunction:: flux.asyncio.FluxEventLoop
