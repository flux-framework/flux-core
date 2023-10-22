=======================
flux_event_subscribe(3)
=======================

.. default-domain:: c

SYNOPSIS
========

.. code-block:: c

  #include <flux/core.h>

  int flux_event_subscribe (flux_t *h, const char *topic);

  int flux_event_unsubscribe (flux_t *h, const char *topic);


DESCRIPTION
===========

Flux events are broadcast across the session, but are only delivered
to handles that subscribe to them by topic. Topic strings consist of
one or more words separated by periods, interpreted as a hierarchical
name space.

``flux_event_subscribe()`` requests that event messages matching *topic*
be delivered via :man3:`flux_recv`. A match consists of a string comparison
of the event topic and the subscription topic, up to the length of the
subscription topic. Thus "foo." matches events with topics "foo.bar"
and "foo.baz", and "" matches all events. This matching algorithm
is inherited from ZeroMQ. Globs or regular expressions are not allowed
in subscriptions, and the period delimiter is included in the comparison.

``flux_event_unsubscribe()`` unsubscribes to a topic. The *topic*
argument must exactly match that provided to ``flux_event_subscribe()``.

Duplicate subscriptions are allowed in the subscription list but
will not result in multiple deliveries of a given message. Each
duplicate subscription requires a separate unsubscribe.

It is not necessary to remove subscriptions with ``flux_event_unsubscribe()``
prior to calling :man3:`flux_close`.


RETURN VALUE
============

These functions return 0 on success. On error, -1 is returned,
and errno is set appropriately.


ERRORS
======

EINVAL
   Some arguments were invalid.

ENOMEM
   Out of memory.


EXAMPLES
========

This example opens the Flux broker, subscribes to heartbeat messages,
displays one, then quits.

.. literalinclude:: example/event.c
  :language: c


RESOURCES
=========

Flux: http://flux-framework.org
