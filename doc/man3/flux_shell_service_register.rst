==============================
flux_shell_service_register(3)
==============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   int flux_shell_service_register (flux_shell_t *shell,
                                    const char *method,
                                    flux_msg_handler_f cb,
                                    void *arg);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

The job shell registers a unique service name with the flux broker on
startup, and posts the topic string for this service in the context of
the ``shell.init`` event. :func:`flux_shell_service_register` allows
registration of a request handler :var:`cb` for subtopic :var:`method` on this
service endpoint, allowing other job shells and/or flux commands to
interact with arbitrary services within a job.


RETURN VALUE
============

Returns -1 on failure, 0 on success.


ERRORS
======

EINVAL
   :var:`shell`, :var:`method` or :var:`cb` is NULL.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_msg_handler_create`
