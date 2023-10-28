=============
flux-event(1)
=============


SYNOPSIS
========

**flux** **event** *COMMAND* [*OPTIONS*]


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

**pub** [-r] [-l] [-s] [-p] *topic* [*payload*]
   Publish an event with optional payload. If payload is specified,
   it is interpreted as raw if the :option:`-r` option is used, otherwise it is
   interpreted as JSON. If the payload spans multiple arguments,
   the arguments are concatenated with one space between them.
   If :option:`-s` is specified, wait for the event's sequence number to be
   assigned before exiting.
   If :option:`-l` is specified, subscribe to the published event and wait for
   it to be received before exiting. :option:`-p` causes the privacy flag to
   be set on the published event.

**sub** *[-c N]* [*topic*] [*topic*\ …​]
   Subscribe to events matching the topic string(s) provided on the
   command line. If none are specified, subscribe to all events.  If
   :option:`-c N` is specified, print the first *N* events on stdout and exit;
   otherwise continue printing events until a signal is received.
   Events are displayed one per line: the topic string, followed by a tab,
   followed by the payload, if any.


RESOURCES
=========

Flux: http://flux-framework.org
