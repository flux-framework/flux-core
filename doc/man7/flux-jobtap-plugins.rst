======================
flux-jobtap-plugins(7)
======================


DESCRIPTION
===========

The *jobtap* interface supports loading of builtin and external
plugins into the job manager broker module. These plugins can be used
to assign job priorities using algorithms other than the default,
assign job dependencies, aid in debugging of the flow of job states,
or generically extend the functionality of the job manager.

Jobtap plugins are defined using the Flux standard plugin format. Therefore
a jobtap plugin should export the single symbol: ``flux_plugin_init()``,
from which calls to ``flux_plugin_add_handler(3)`` should be used to
register functions which will be called for the callback topic strings
described in the :ref:`callback_topics` section below.

Each callback function uses the Flux standard plugin callback form, e.g.::

   int callback (flux_plugin_t *p,
                 const char *topic,
                 flux_plugin_arg_t *args,
                 void *arg);

where ``p`` is the handle for the current *jobtap* plugin, ``topic`` is
the *topic string* for the currently invoked callback, ``args`` contains
a set of plugin arguments which may be unpacked with the
``flux_plugin_arg_unpack(3)`` call, and ``arg`` is any opaque argument
passed along when registering the handler.

Multiple plugins may be loaded in the job-manager simultaneously. In this
case, all matching handlers are called in all loaded plugins in the order
in which they were loaded. For more information about loading plugins
see the :ref:`configuration` section below or the ``flux-jobtap(1)``
manpage.

JOBTAP PLUGIN ARGUMENTS
=======================

For job-specific callbacks, all job data is passed to the plugin via
the ``flux_plugin_arg_t *args``, and return data is sent back to the
job manager via the same ``args``. Incoming arguments may be unpacked
using ``flux_plugin_arg_unpack(3)``, e.g.::

   rc = flux_plugin_arg_unpack (args, FLUX_PLUGIN_ARG_IN,
                                "{s{s:o}, s:I}",
                                "jobspec", "resources", &resources,
                                "id", &id);

will unpack the ``resources`` section of jobspec and the jobid into
``resources`` and ``id`` respectively.

The full list of available args includes the following:

========== ==== ==========================================
name       type description
========== ==== ==========================================
jobspec    o    jobspec with environment redacted
id         I    jobid
state      i    current job state
prev_state i    previous state (``job.state.*`` callbacks)
userid     i    userid
urgency    i    current urgency
priority   I    current priority
t_submit   f    submit timestamp in floating point seconds
entry      o    posted eventlog entry, including context
========== ==== ==========================================

Return arguments can be packed using the ``FLUX_PLUGIN_ARG_OUT`` and
optionally ``FLUX_PLUGIN_ARG_UPDATE`` flags. For example to return
a priority::

   rc = flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT,
                              "{s:I}",
                              "priority", (int64_t) priority);

While a job is pending, *jobtap* plugin callbacks may also add job
annotations by returning a value for the ``annotations`` key::

   flux_plugin_arg_pack (args, FLUX_PLUGIN_ARG_OUT|FLUX_PLUGIN_ARG_UPDATE,
                         "{s:{s:s}}",
                         "annotations", "test", value);

.. _callback_topics:

CALLBACK TOPICS
===============

The following callback "topic strings" are currently provided by the
*jobtap* interface:

job.validate
  The ``job.validate`` topic allows a plugin to reject a job before
  it is introduced to the job manager. A rejected job will result in
  a job submission error in the submitting client, and any job data in
  the KVS will be purged. No further callbacks will be made for rejected
  jobs. Note: If a job is not rejected, then the ``job.new`` callback will
  be invoked immediately after ``job.validate``. This allows limits or
  other validation to be implemented in the ``job.validate`` callback,
  but accounting for those limits should be confined to the ``job.new``
  callback, since ``job.new`` may also be called during job-manager
  restart or plugin reload.

job.dependency.*
  The ``job.dependency.*`` topic allows a dependency plugin to notify the
  job-manager that it handles a given dependency _scheme_. The job-manager
  will scan the ``attirbutes.system.dependencies`` array, if provided, and
  issue a ``job.dependency.SCHEME`` callback for each listed dependency.
  If no plugin has registered for ``SCHEME``, then the job is rejected.
  The plugin should then call ``flux_jobtap_dependency_add(3)`` to add
  a new named dependency to the job (if necessary). Jobs with dependencies
  will remain in the ``DEPEND`` state until all dependencies are removed
  with a corresponding call to ``flux_jobtap_dependency_remove(3)``. See
  ``job.state.depend`` below for more information about dependencies.
  If there is an error in the dependency specification, the job may be
  rejected with ``flux_jobtap_reject_job(3)`` and a negative return code 
  from the callback.

job.new
  The ``job.new`` topic is used by the job manager to notify a jobtap plugin
  about a newly introduced job. This call may be made in three different
  situations:

    1. on job job submission
    2. when the job manager is restarted and has reloaded a job from the KVS
    3. when a new jobtap plugin is loaded

  In case 1 above, the job state will always be ``FLUX_JOB_STATE_NEW``, while
  jobs in cases 2 and 3 can be in any state except ``FLUX_JOB_STATE_INACTIVE``.

job.state.*
  The ``job.state.*`` callbacks are made just after a job state transition.
  The callback is made after the state has been published to the job's
  eventlog, but before any action has been taken on that state (since the
  action could involve immediately transitioning to a new state)

job.state.depend
  The callback for ``FLUX_JOB_STATE_DEPEND`` is the final place from which
  a plugin may add dependencies to a job. Dependencies are added via
  the ``flux_jobtap_dependency_add()`` function. This function allows a
  named dependency to be attached to a job. Jobs with dependencies will
  remain in the ``DEPEND`` state until all dependencies are removed with
  a corresponding call the ``flux_jobtap_dependency_remove()``. A dependency
  may only be used once. A second call to ``flux_jobtap_dependency_add()``
  with the same dependency description will return ``EEXIST``, even if
  the dependency was subsequently removed. (This allows idempotent operation
  of plugin-managed dependencies for job-manager or plugin restart).

job.state.priority
  The callback for ``FLUX_JOB_STATE_PRIORITY`` is special, in that a plugin
  must return a priority at the end of the callback (if the plugin is
  a priority-managing plugin). If no priority is returned from this callback,
  then the job manager assumes the plugin does not set job priorities,
  and will take default action. If the job priority is not available, the
  plugin should instead use ``flux_jobtap_priority_unavail()`` to indicate
  that the priority cannot be set. Jobs that do not have a priority will
  remain in the PRIORITY state until a priority is assigned, so a plugin
  should arrange for the priority to be set asynchronously using 
  ``flux_jobtap_reprioritize_job()``).

job.priority.get
  The job manager calls the ``job.priority.get`` topic whenever it wants
  to update the job priority of a single job. The plugin should return a
  priority immediately, but if one is not available when a job is in
  the PRIORITY state, the plugin may use ``flux_jobtap_priority_unavail()``
  to indicate the priority is not available. Returning an unavailable
  priority in the SCHED state is an error and it will be logged, but
  otherwise ignored.

.. _configuration:

CONFIGURATION
=============

Job-manager plugin configuration is defined in the ``job-manager.plugins``
section of the Flux TOML configuration file. This section is an array of
plugin directives which include the following keys:

load
  Load a plugin matching the given filename into the job-manager. If the
  path is not absolute, then the first plugin matching the job-manager
  searchpath will be loaded.

conf
  With load only, pass an optional configuration table to the loaded plugin.

remove
  Remove all plugins matching the value. The value may be a glob(7). If
  ``remove`` appears with ``load``, plugin removal is always handled first.
  The special value ``all`` is a synonym for ``*``, but will not error when
  no plugins match.

For example

::

    [job-manager]
    plugins = [
       {
         load = "priority-custom.so",
         conf = {
            job-limit = 100,
            size-limit = 128
         }
       }
    ]

The list of loaded jobtap plugins may also be queried and controlled at
runtime with the ``flux-jobtap(1)`` command


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux-jobtap(1)

