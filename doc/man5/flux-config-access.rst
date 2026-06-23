=====================
flux-config-access(5)
=====================


DESCRIPTION
===========

Flux normally denies access to all users except the instance owner (the user
running the Flux instance).  The system instance, however, runs as the
``flux`` user and permits limited access to guests, such as submitting work
and manipulating their own jobs.

The ``access`` table is required for a multi-user Flux system instance and may
contain the following keys:


KEYS
====

allow-guest-user
   (optional) Boolean value to allow guest users to connect and assigns them
   the ``user`` role.  If set to false or not present, connection attempts
   by guests fail with EPERM.

allow-root-owner
   (optional) Boolean value to assign ``owner`` role to root user.  If set to false
   or not present, root is treated like any other guest.

private-mode
   (optional) Boolean value to limit visibility of job data to guests.
   When true, job queries from guests are limited to their own jobs and
   aggregate job statistics are unavailable to them. The instance owner is
   unaffected. This key is only meaningful when ``allow-guest-user`` is true.
   If set to false or not present, guests may view all users' jobs.


EXAMPLE
=======

::

   [access]
   allow-guest-user = true
   allow-root-owner = true
   private-mode = true


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config`
