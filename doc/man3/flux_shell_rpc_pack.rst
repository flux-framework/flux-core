======================
flux_shell_rpc_pack(3)
======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   flux_future_t *flux_shell_rpc_pack (flux_shell_t *shell,
                                       const char *method,
                                       int shell_rank,
                                       int flags,
                                       const char *fmt,
                                       ...);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

Send a remote procedure call :var:`method` to another shell in the same
job at shell rank :var:`shell_rank`.


RETURN VALUE
============

Returns NULL on failure.


ERRORS
======

EINVAL
   :var:`shell`, :var:`method` or :var:`fmt` are NULL, or if :var:`rank` is
   less than 0.


RESOURCES
=========

Flux: http://flux-framework.org
