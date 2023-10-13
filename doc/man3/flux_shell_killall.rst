=====================
flux_shell_killall(3)
=====================


SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   void flux_shell_killall (flux_shell_t *shell, int sig);


DESCRIPTION
===========

Sends the signal ``sig`` to all processes running in ``shell``. No errors are
set, but the call returns immediately if ``shell`` is NULL or if ``sig`` is
zero or negative.


RETURN VALUE
============

None.


ERRORS
======

None.


RESOURCES
=========

Flux: http://flux-framework.org
