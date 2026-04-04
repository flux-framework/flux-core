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
functions, e.g. :ref:`flux.job.submit <python_flux_job_submit_func>`.
Jobspec objects define everything about a job, including the job's
resources, executable, working directory, environment, and stdio streams.

``JobspecV1`` provides two layers of factory methods. The low-level
methods — :meth:`~flux.job.JobspecV1.from_command`,
:meth:`~flux.job.JobspecV1.from_batch_command`, and
:meth:`~flux.job.JobspecV1.from_nest_command` — build the RFC 14 jobspec
structure directly, accepting raw values for environment, resource limits,
and duration. Further customization of the resulting jobspec is done via
various setters, getters, and methods of :meth:`~flux.job.JobspecV1`, or
further processing by :meth:`~flux.job.JobspecV1.apply_options`, which is
a convenience method used by command-line tools to process common options
and CLI plugin arguments.

High-level methods -- :meth:`~flux.job.JobspecV1.from_submit`,
:meth:`~flux.job.JobspecV1.from_alloc`, and :meth:`~flux.job.JobspecV1.from_batch`
are also available which give callers full access to the same options
available in the command-line tools :man1:`flux-submit`, :man1:`flux-alloc`,
and :man1:`flux-batch` respectively.  These methods are equivalent to calling
a low-level method and :meth:`~flux.job.JobspecV1.apply_options`
with the appropriate ``prog`` in a single step. They use command-line flag
aligned parameter names (``ntasks``, ``nodes``, ``time_limit``) and pass any
remaining keyword arguments to :meth:`~flux.job.JobspecV1.apply_options`. The
result is a jobspec equivalent to what ``flux submit``, ``flux alloc``, or
``flux batch`` would produce.


.. autoclass:: flux.job.Jobspec
	:members:

.. autoclass:: flux.job.JobspecV1
	:show-inheritance:
	:members:


High-level factory methods
~~~~~~~~~~~~~~~~~~~~~~~~~~

:meth:`~flux.job.JobspecV1.from_submit`,
:meth:`~flux.job.JobspecV1.from_alloc`, and
:meth:`~flux.job.JobspecV1.from_batch` are the recommended starting
point for most Python code. Each wraps the corresponding low-level
method and calls :meth:`~flux.job.JobspecV1.apply_options` internally,
so all :meth:`~flux.job.JobspecV1.apply_options` keyword arguments
(``env``, ``rlimit``, ``time_limit``, ``dependency``, ``shell_options``,
``attributes``, and more) can be passed directly, as can any CLI plugin
option dests. The resulting jobspec is passed to :func:`~flux.job.submit`
or :func:`~flux.job.submit_async` to actually run the job.

Build a jobspec for a 16-task command and submit it:

.. code-block:: python

    import flux
    import flux.job
    from flux.job import JobspecV1

    js = JobspecV1.from_submit(
        ["myapp", "--input", "data.h5"],
        ntasks=16,
        cores_per_task=4,
        time_limit="2h",
        dependency=["afterok:f1234abcd"],
        shell_options={"verbose": 1},
        attributes={"system.queue": "gpu"},
    )
    jobid = flux.job.submit(flux.Flux(), js)

Build a jobspec for a nested Flux instance on four nodes:

.. code-block:: python

    js = JobspecV1.from_alloc(
        nodes=4,
        time_limit="1h",
        conf={"resource": {"noverify": True}},
    )
    jobid = flux.job.submit(flux.Flux(), js)

Build a jobspec for a batch script from a file or inline content:

.. code-block:: python

    # From a file — job name defaults to the filename
    js = JobspecV1.from_batch("/path/to/script.sh", nodes=4)

    # Inline script content — use the content= keyword argument
    js = JobspecV1.from_batch(
        content="#!/bin/bash\nflux run -n8 myapp\n",
        nslots=8,
        time_limit="30m",
        output="job-{{id}}.out",
    )
    jobid = flux.job.submit(flux.Flux(), js)

See `Applying options to jobspecs`_ below for details on passing shell
options, jobspec attributes, environment rules, resource limits, and CLI
plugin options through these methods.


Applying options to jobspecs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:meth:`~flux.job.JobspecV1.apply_options` can be called on any jobspec,
including those created with the lower-level factory methods. It modifies
the jobspec in-place and returns *self* so calls can be chained.

**Shell options and jobspec attributes**

``shell_options`` sets job-shell options; ``attributes`` sets jobspec
attributes. Both accept plain Python dicts.

.. code-block:: python

    from flux.job import JobspecV1

    js = JobspecV1.from_command(["hostname"]).apply_options(
        shell_options={"verbose": 1},
        attributes={"system.queue": "batch", ".user.comment": "my job"},
    )

Note that the above is equivalent to:

.. code-block:: python

    from flux.job import JobspecV1

    js = JobspecV1.from_submit(
        ["hostname"],
        shell_options={"verbose": 1},
        attributes={"system.queue": "batch", ".user.comment": "my job"},
    )


**Environment and resource limits**

``env`` accepts filter rules applied to the submitter's environment.
``rlimit`` propagates resource limits. The high-level factory methods
(:meth:`~flux.job.JobspecV1.from_submit`,
:meth:`~flux.job.JobspecV1.from_alloc`,
:meth:`~flux.job.JobspecV1.from_batch`) always propagate the full environment
and default resource limits even when neither is specified — matching CLI
behavior. When calling :meth:`~flux.job.JobspecV1.apply_options` directly,
``env`` and ``rlimit`` are only applied when explicitly passed; a call that
omits both leaves any previously set environment and rlimits unchanged.

.. code-block:: python

    js.apply_options(
        env=["MY_RANK={{rank}}", "-LD_PRELOAD", "IMPORTANT_VAR"],
        rlimit=["-*", "nofile=65536"],
    )

**Using CLI plugin options**

When ``prog`` is set, CLI plugins registered for that command are loaded
and their ``modify_jobspec()`` hooks are invoked. Run ``flux <cmd> --help``
to see what plugins are loaded — their options appear under "Options provided
by plugins". The kwarg dest for each option is the flag name with the leading
``--`` removed and dashes replaced by underscores (e.g. ``--amd-gpumode`` →
``amd_gpumode``):

.. code-block:: python

    # --amd-gpumode is listed under "Options provided by plugins" in
    # "flux submit --help" output; its dest is amd_gpumode
    js = JobspecV1.from_submit(
        ["myapp"],
        ntasks=8,
        amd_gpumode="TPX",
    )

    # Or apply to an existing jobspec:
    js.apply_options(prog="submit", amd_gpumode="TPX")

For programmatic discovery of available plugin option dests:

.. code-block:: python

    from flux.cli.plugin import CLIPluginRegistry

    for opt in CLIPluginRegistry("submit").options:
        print(f"{opt.name} -> dest: {opt.dest}")


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


The simplest way to interact with Flux is with the synchronous functions listed
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

.. autofunction:: flux.job.get_job


``get_job``
~~~~~~~~~~~

After submitting a job, if you quickly want to see information for it,
you can use ``flux.job.get_job``. Here is an example:

.. code:: python

    import flux
    import flux.job

    # It's encouraged to create a handle to use across commands
    handle = flux.Flux()

    jobspec = flux.job.JobspecV1.from_command(["sleep", "60"])
    jobid = flux.job.submit(handle, jobspec)
    job_meta = flux.job.get_job(handle, jobid)

    {
        "job": {
            "id": 676292747853824,
            "userid": 0,
            "urgency": 16,
            "priority": 16,
            "t_submit": 1667760398.4034982,
            "t_depend": 1667760398.4034982,
            "state": "SCHED",
            "name": "sleep",
            "ntasks": 1,
            "ncores": 1,
            "duration": 0.0
        }
    }

If the jobid you are asking for does not exist, ``None`` will be returned.
For the interested user, this is a courtesy function that wraps using the identifier
to create an RPC object, serializing that to string, and loading as JSON. 
Since it is likely you, as the user, will be interacting with ``flux.job``, it
is also logical you would look for this function to retrieve the job on the same
module.

``result`` vs ``wait``
~~~~~~~~~~~~~~~~~~~~~~

Both ``flux.job.result`` and ``flux.job.wait`` return when a job has completed.
However, ``wait`` only works on jobs which have been submitted
with the ``waitable`` flag, and the ability to set that flag is
restricted to instance owners.


Asynchronous interfaces
-----------------------

There are two primary asynchronous interfaces to job manipulations. The first is
an event-loop interface, which is closer to the native C interface, and consists of
functions like ``flux.job.submit_async`` and ``flux.job.result_async`` (note the
functions are the same as in the synchronous interface, only with an "_async"
suffix). The second is an interface which is almost identical to the
`concurrent.futures <https://docs.python.org/3/library/concurrent.futures.html>`_
interface in Python's standard library, and it consists of the
``flux.job.FluxExecutor`` class. Both interfaces deal in callbacks and
futures, the difference being that the ``FluxExecutor`` is designed so that
all futures fulfill in the background, and there is no need for user code
to enter the Flux event loop, while the event-loop-based interface
requires the user to call into the Flux event loop in order for futures
to fulfill and for callbacks to trigger.

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
		job = fut.get_info()
		result = job.result.lower()
		print(f"{job.id}: {result} with returncode {job.returncode}")

	flux_handle = flux.Flux()
	jobspec = flux.job.JobspecV1.from_command(["/bin/true"])
	for _ in range(5):
		# submit 5 futures and attach callbacks to each one
		submit_future = flux.job.submit_async(flux_handle, jobspec)
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
