=======================
flux_jobtap_get_flux(3)
=======================


SYNOPSIS
========

::

   #include <flux/core.h>
   #include <flux/jobtap.h>

::

   flux_t *flux_jobtap_get_flux (flux_plugin_t *p);

::

   int flux_jobtap_service_register (flux_plugin_t *p,
                                     const char *method,
                                     flux_msg_handler_f cb,
                                     void *arg);

::

   int flux_jobtap_reprioritize_all (flux_plugin_t *p);

::

   int flux_jobtap_reprioritize_job (flux_plugin_t *p,
                                     flux_jobid_t id,
                                     unsigned int priority);

::

   int flux_jobtap_priority_unavail (flux_plugin_t *p,
                                     flux_plugin_arg_t *args);


DESCRIPTION
===========

These interfaces are used by Flux *jobtap* plugins which are used to
extend the job manager broker module.

``flux_jobtap_get_flux()`` returns the job manager's Flux handle given
the plugin's ``flux_plugin_t *``. This can be used by a *jobtap* plugin
to send RPCs, schedule timer watchers, or other asynchronous work.

``flux_jobtap_service_register()`` registers a service name ``method``
under the job manager which will be handled by the provided message
handler ``cb``.  The constructed service name will be
``job-manager.<name>.<method>`` where ``name`` is the name of the plugin
as returned by ``flux_plugin_get_name(3)``. As such, this call may
fail if the *jobtap* plugin has not yet set a name for itself using
``flux_plugin_set_name(3)``.

``flux_jobtap_reprioritize_all()`` requests that the job manager begin
reprioritization of all pending jobs, i.e. jobs in the PRIORITY and
SCHED states. This will result on each job having a ``job.priority.get``
callback invoked on it.

``flux_jobtap_reprioritize_job()`` allows a *jobtap* plugin to asynchronously
assign the priority of a job.

``flux_jobtap_priority_unavail()`` is a convenience function which may
be used by a plugin in the ``job.state.priority`` priority callback to
indicate that a priority for the job is not yet available. It can be
called as::

   return flux_jobtap_priority_unavail (p, args);


RETURN VALUE
============

``flux_jobtap_get_flux()`` returns a ``flux_t *`` handle on success. ``NULL``
is returned with errno set to ``EINVAL`` if the supplied ``flux_plugin_t *p``
is not a jobtap plugin handle.

The remaining functions return 0 on success, -1 on failure.

RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

flux-jobtap-plugins(7)
