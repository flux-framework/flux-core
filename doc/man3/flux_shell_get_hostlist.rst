==========================
flux_shell_get_hostname(3)
==========================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/shell.h>
   #include <errno.h>

   const struct hostlist * flux_shell_get_hostlist (flux_shell_t *shell);

Link with :command:`-lflux-core -lflux-hostlist`.

DESCRIPTION
===========

:func:`flux_shell_get_hostlist` returns the list of hosts assigned to the
current job in ``struct hostlist`` form. This hostlist can be used to
map job node IDs or job shell ranks to hostnames using the interfaces
exported in ``libflux-hostlist.so``.


RETURN VALUE
============

This function returns a pointer to the shell's internal :type:`struct hostlist`
or ``NULL`` on failure with :var:`errno` set.


ERRORS
======

EINVAL
   if :var:`shell` is NULL or the function is called before the hostlist is
   available to the job shell.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_34`
