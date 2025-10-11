==========================
flux_module_set_running(3)
==========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_module_set_running (flux_t *h);

  bool flux_module_debug_test (flux_t *h, int flag, bool clear);

  int flux_module_config_request_decode (const flux_msg_t *msg,
                                         flux_conf_t **conf);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_module_set_running` sets the broker module state to RUNNING.
A module load request blocks until the module reaches this state to
ensure that any fatal module initialization errors are reported.
The transition to RUNNING occurs automatically when the reactor is
entered.  This function can set the state to RUNNING to finalize
the load request earlier if necessary.

:func:`flux_module_debug_test` tests and optionally clears flags in the
RFC 5 module debug mask.  The module debug mask is normally clear, but
the :option:`flux module debug` command may be used to alter the mask
to create test conditions.  The meanings of specific the flag bits are
module-dependent.

:func:`flux_module_config_request_decode` decodes a ``config-reload``
request message sent by the broker.  It creates a config object :var:`conf`
that contains the proposed config.  This is helpful when a broker module
overrides the default request handler for ``config-reload`` in order to
respond to dynamic configuration changes.

RETURN VALUE
============

:func:`flux_module_set_running` returns 0 on success, or -1 on failure with
On error, NULL is returned, and :var:`errno` is set.
:var:`errno` set.

:func:`flux_module_debug_test` returns true if the specified debug flag is
set.

:func:`flux_module_config_request_decode` returns 0 on success, or -1
on failure with :var:`errno` set.

ERRORS
======

EINVAL
   Invalid argument.

ENOMEM
   Out of memory.

RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

:doc:`rfc:spec_5`

SEE ALSO
========

:man1:`flux-module`,
:man1:`flux-config`,
:man3:`flux_conf_create`,
:man5:`flux-config`.
