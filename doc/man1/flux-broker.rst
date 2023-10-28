==============
flux-broker(1)
==============


SYNOPSIS
========

**flux-broker** [*OPTIONS*] [*initial-program* [*args...*]]

DESCRIPTION
===========

.. program:: flux broker

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

.. option:: -h, --help

   Summarize available options.

.. option:: -v, --verbose

   Be annoyingly chatty.

.. option:: -S, --setattr=ATTR=VAL

   Set initial value for broker attribute.

.. option:: -c, --config-path=PATH

   Set the PATH to broker configuration. If PATH is a directory, then
   read all TOML files from that directory. If PATH is a file, then load
   configuration as JSON if the file extension is ``.json``, otherwise
   load the file as TOML.


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man7:`flux-broker-attributes`
