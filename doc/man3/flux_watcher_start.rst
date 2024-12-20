=====================
flux_watcher_start(3)
=====================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  void flux_watcher_start (flux_watcher_t *w);

  void flux_watcher_stop (flux_watcher_t *w);

  bool flux_watcher_is_active (flux_watcher_t *w);

  void flux_watcher_unref (flux_watcher_t *w);

  void flux_watcher_ref (flux_watcher_t *w);

  bool flux_watcher_is_referenced (flux_watcher_t *w);

  void flux_watcher_destroy (flux_watcher_t *w);

  double flux_watcher_next_wakeup (flux_watcher_t *w);

Link with :command:`-lflux-core`.

DESCRIPTION
===========

:func:`flux_watcher_start` activates a :type:`flux_watcher_t` object :var:`w`
so that it can receive events. If :var:`w` is already active, the call has no
effect.  This may be called from within a :type:`flux_watcher_f` callback.

:func:`flux_watcher_stop` deactivates a :type:`flux_watcher_t` object :var:`w`
so that it stops receiving events. If :var:`w` is already inactive, the call
has no effect.  This may be called from within a :type:`flux_watcher_f`
callback.

:func:`flux_watcher_is_active` returns a true value if the watcher is active
(i.e. it has been started and not yet stopped) and false otherwise.

:func:`flux_watcher_unref` drops the watcher's reference on the reactor.
This function is idempotent.  :func:`flux_reactor_run` normally runs until
there are no more active watchers with references.

:func:`flux_watcher_ref` restores the watcher's reference on the reactor.
This function is idempotent.

:func:`flux_watcher_is_referenced` returns true if the watcher is referenced.
All watchers are referenced unless updated with :func:`flux_watcher_unref`.

.. note::
  All watchers in the public API support :func:`flux_watcher_unref`, but some
  specialized watchers internal to flux-core do not.  If in doubt about whether
  the call had any effect, check with :func:`flux_watcher_is_referenced`.

:func:`flux_watcher_destroy` destroys a :type:`flux_watcher_t` object :var:`w`,
after stopping it. It is not safe to destroy a watcher object within a
:type:`flux_watcher_f` callback.

:func:`flux_watcher_next_wakeup` returns the absolute time that the watcher
is supposed to trigger next. This function only works for :var:`timer` and
:var:`periodic` watchers, and will return a value less than zero with
:var:`errno` set to ``EINVAL`` otherwise.


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man3:`flux_reactor_create`
