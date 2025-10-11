================
flux_get_conf(3)
================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  flux_conf_t *flux_get_conf (flux_t *h);

  int flux_set_conf_new (flux_t *h, const flux_conf_t *conf);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_get_conf` retrieves a :type:`flux_conf_t` object previously
associated with the broker handle :var:`h` by a call to
:func:`flux_set_conf_new`.  If one has not been previously associated,
an empty :type:`flux_conf_t` object is associated on demand.
In broker modules, the current broker configuration is pre-associated.

:func:`flux_set_conf_new` associates a :type:`flux_conf_t` object :var:`conf`
with a broker handle :var:`h`, stealing the reference to :var:`conf`.
If one has been previously associated, its reference is dropped first.

RETURN VALUE
============

:func:`flux_get_conf` returns a :type:`flux_conf_t` object on success.
On error, NULL is returned, and :var:`errno` is set.

:func:`flux_set_conf_new` returns 0 on success, or -1 on failure with
:var:`errno` set.

ERRORS
======

ENOMEM
   Out of memory.

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-config`, :man3:`flux_conf_create`, :man5:`flux-config`
