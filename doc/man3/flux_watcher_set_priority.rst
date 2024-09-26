============================
flux_watcher_set_priority(3)
============================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

   #include <flux/core.h>

   flux_watcher_t *flux_watcher_set_priority (flux_watcher_t *w,
                                              int priority);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_watcher_set_priority` sets the priority on the watcher.
Higher priority watchers run first.  The range of priorities is from
-2 to 2, with the default being 0.  If the priority is out
of range, the max or min value is set.  The priority should only be
set when the watcher is stopped.

This function is a no-op if the underlying watcher does not support priorities.
Currently only the check watcher supports priorities.


RESOURCES
=========

.. include:: common/resources.rst

libev: http://software.schmorp.de/pkg/libev.html


SEE ALSO
========

:man3:`flux_check_watcher_create`
