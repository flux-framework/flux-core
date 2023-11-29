=============
flux-event(1)
=============


SYNOPSIS
========

| **flux** **event** **pub** [*--raw*] [*--synchronous*] [*--private*] *topic* [*payload*]
| **flux** **event** **sub** [*--count=N*] *topic...*


DESCRIPTION
===========

Flux events are messages that are broadcast throughout the Flux instance
with publish/subscribe semantics. Each event message has a *topic string*
and an optional *payload*.

Subscriptions are by topic string. A subscription topic of length *N*
matches an event if the first *N* characters of the event topic
are identical to that of the subscription. For example the event topic
*a.b.c* is matched by the subscription topic *a.b.c*, *a.b*, or *a*.
A subscription to the empty string matches all events.


COMMANDS
========

pub
---

.. program:: flux event pub

Publish an event on *topic* with optional *payload*. If payload is specified,
it is interpreted as JSON unless other options are selected.  If the payload
spans multiple arguments, the arguments are concatenated with one space
between them.

.. option:: -r, --raw

  Interpret event payload as raw instead of JSON.

.. option:: -s, --synchronous

  Wait for the event's sequence number to be assigned before exiting.

.. option:: -l, --loopback

  Subscribe to the published event and wait for it to be received before
  exiting.

.. option:: -p, --private

  Set the privacy flag on the published event.

Example: publish an event with topic ``foo.hello`` and no payload::

  flux event pub foo.hello

sub
---

.. program:: flux event sub

Subscribe to events matching the topic string(s) provided on the
command line. If none are specified, subscribe to all events.
Events are displayed one per line: the topic string, followed by a tab,
followed by the payload, if any.

.. option:: -c, --count=N

 Print the first *N* events on stdout and exit.  Otherwise events are
 processed until a signal is received.

Example: subscribe to all events with topic prefix of ``foo.``::

  flux event sub foo.


RESOURCES
=========

.. include:: common/resources.rst


FLUX RFC
========

:doc:`rfc:spec_3`
