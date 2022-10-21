=========================
flux_get_process_scope(3)
=========================


SYNOPSIS
========

#include <flux/core.h>

int flux_get_process_scope (flux_process_scope_t *scope);


DESCRIPTION
===========

``flux_get_process_scope()`` determines if the calling process is
running under one of the following situations returned via ``scope``.

FLUX_PROCESS_SCOPE_NONE
    Process is not running under flux.

FLUX_PROCESS_SCOPE_SYSTEM_INSTANCE
    Process is running under the system instance.

FLUX_PROCESS_SCOPE_INITIAL_PROGRAM
    Process is the initial program running under a non-system instance.

FLUX_PROCESS_SCOPE_JOB
    Process is a job running under a non-system instance.


RETURN VALUE
============

``flux_get_process_scope()`` returns zero on success . On error, -1 is
returned, and errno is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.


RESOURCES
=========

Flux: http://flux-framework.org

