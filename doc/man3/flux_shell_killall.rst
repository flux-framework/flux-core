=====================
flux_shell_killall(3)
=====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>

   void flux_shell_killall (flux_shell_t *shell, int sig);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

Sends the signal :var:`sig` to all processes running in :var:`shell`. No
errors are set, but the call returns immediately if :var:`shell` is NULL
or if :var:`sig` is zero or negative.


RETURN VALUE
============

None.


ERRORS
======

None.


RESOURCES
=========

.. include:: common/resources.rst
