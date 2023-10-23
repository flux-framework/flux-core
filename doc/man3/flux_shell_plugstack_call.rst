============================
flux_shell_plugstack_call(3)
============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   int flux_shell_plugstack_call (flux_shell_t *shell,
                                  const char *topic,
                                  flux_plugin_arg_t *args);


DESCRIPTION
===========

The job shell implements a flexible plugin architecture which allows
registration of one or more callback functions on arbitrary topic
names. The stack of functions "listening" on a given topic string is
called the "plugin stack". :func:`flux_shell_plugstack_call` exports the
ability to call into the plugin stack so that plugins can invoke
callbacks from other plugins.


RETURN VALUE
============

Returns 0 on success and -1 on failure, setting :var:`errno`.


ERRORS:
=======

EINVAL
   :var:`shell` or :var:`topic` are NULL.


RESOURCES
=========

Flux: http://flux-framework.org
