====================
flux_core_version(3)
====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   int flux_core_version (int *major, int *minor, int *patch);

   const char *flux_core_version_string (void);


DESCRIPTION
===========

flux-core defines several macros and functions to let API users determine
the version they are working with. A version has three components
(*major*, *minor*, *patch*), accessible with the following macros:

FLUX_CORE_VERSION_MAJOR
   (integer) incremented when there are incompatible API changes

FLUX_CORE_VERSION_MINOR
   (integer) incremented when functionality is added in a backwards-compatible
   manner

FLUX_CORE_VERSION_PATCH
   (integer) incremented when bug fixes are added in a backwards-compatible manner

These definitions conform to the *semantic versioning* standard (see below).
In addition, the following convenience macros are available:

FLUX_CORE_VERSION_HEX
   (hex) the three versions combined into a three-byte integer value,
   useful for comparing versions with *<*, *=*, and *>* operators.

FLUX_CORE_VERSION_STRING
   (string) the three versions above separated by periods, with optional
   :linux:man1:`git-describe` suffix preceded by a hyphen, if the version is a
   development snapshot.

Note that major version zero (0.y.z) is for initial development.
Under version zero, the public API should not be considered stable.

Functions are also available to access the same values. While the header
macros tell what version of flux-core your program was compiled against,
the functions tell what version your program is dynamically linked with.

:func:`flux_core_version` sets *major*, *minor*, and *patch* to the values of
the macros above. If any parameters are NULL, no assignment is attempted.

:func:`flux_core_version_string` returns the string value.


RETURN VALUE
============

:func:`flux_core_version` returns the hex version.

:func:`flux_core_version_string` returns the version string


ERRORS
======

These functions cannot fail.


RESOURCES
=========

Flux: http://flux-framework.org

Semantic Versioning 2.0.0: http://semver.org
