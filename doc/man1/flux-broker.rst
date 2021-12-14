.. flux-help-description: Invoke Flux message broker daemon

==============
flux-broker(1)
==============


SYNOPSIS
========

**flux-broker** [*OPTIONS*] [*initial-program* [*args...*]]

DESCRIPTION
===========

flux-broker(1) is a distributed message broker daemon that provides
communications services within a Flux instance. It may be
launched as a parallel program under Flux or other resource managers
that support PMI.

Resource manager services are implemented as dynamically loadable
modules.

Brokers within a Flux instance are interconnected using
ZeroMQ sockets, and each is assigned a rank from 0 to size - 1.
The rank 0 node is the root of a tree-based overlay network.
This network may be accessed by Flux commands and modules
using Flux API services.

A logging service aggregates Flux log messages across the instance and
emits them to a configured destination on rank 0.

After its overlay network has completed wire-up, flux-broker(1)
starts the initial program on rank 0. If none is specified on
the broker command line, an interactive shell is launched.


OPTIONS
=======

**-h, --help**
   Summarize available options.

**-v, --verbose**
   Be annoyingly chatty.

**-S, --setattr**\ =\ *ATTR=VAL*
   Set initial value for broker attribute.


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

:man7:`flux-broker-attributes`
