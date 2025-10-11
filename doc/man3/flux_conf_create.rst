===================
flux_conf_create(3)
===================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  flux_conf_t *flux_conf_create (void);

  const flux_conf_t *flux_conf_incref (const flux_conf_t *conf);

  void flux_conf_decref (const flux_conf_t *conf);

  flux_conf_t *flux_conf_copy (const flux_conf_t *conf);

  int flux_conf_unpack (const flux_conf_t *conf,
                        flux_error_t *error,
                        const char *fmt,
                        ...);

  flux_conf_t *flux_conf_pack (const char *fmt, ...);

  flux_conf_t *flux_conf_parse (const char *path, flux_error_t *error);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

Flux configuration is represented by a :type:`flux_conf_t` object.

:func:`flux_conf_create` creates an empty object with a reference
count of one.

:func:`flux_conf_incref` increments the object reference count.
:func:`flux_conf_decref` decrements the object reference count
and destroys it when the count reaches zero.

:func:`flux_conf_copy` duplicates an object.

:func:`flux_conf_unpack` unpacks an object using Jansson
:func:`json_unpack` style arguments.

:func:`flux_conf_pack` creates an object using Jansson
:func:`json_pack` style arguments.

:func:`flux_conf_parse` parse a TOML configuration file at :var:`path`
and creates a config object that contains the result.

ENCODING JSON PAYLOADS
======================

.. include:: common/json_pack.rst

DECODING JSON PAYLOADS
======================

.. include:: common/json_unpack.rst

RETURN VALUE
============

:func:`flux_conf_create`, :func:`flux_conf_copy`, :func:`flux_conf_pack`,
and :func:`flux_conf_incref` return a :type:`flux_conf_t` object on success.
On error, NULL is returned, and :var:`errno` is set.

:func:`flux_conf_unpack` returns 0 on success, or -1 on failure with
:var:`errno` set.

:func:`flux_conf_parse` returns 0 on success.  On error, it returns -1,
:var:`errno` set, and if :var:`error` is non-NULL, it is filled with
a human readable error message.

ERRORS
======

EINVAL
   Invalid argument.

ENOMEM
   Out of memory.

RESOURCES
=========

.. include:: common/resources.rst

TOML: Tom's Oblivious, Minimal Language https://github.com/toml-lang/toml

SEE ALSO
========

:man3:`flux_get_conf`

