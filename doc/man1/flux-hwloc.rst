.. flux-help-command: hwloc
.. flux-help-description: Control/query resource-hwloc service

=============
flux-hwloc(1)
=============


SYNOPSIS
========

**flux** **hwloc** **info** [*OPTIONS*]

**flux** **hwloc** **lstopo** [*lstopo-OPTIONS*]

**flux** **hwloc** **reload** [*OPTIONS*] [*DIR*]

**flux** **hwloc** **topology** [*OPTIONS*]


DESCRIPTION
===========

The **flux-hwloc** utility provides a mechanism to collect
system topology from each flux-broker using the Portable Hardware
Locality (hwloc) library, and to query the resulting data
stored in the Flux Key Value Store (KVS).


COMMANDS
========

**flux hwloc** requires a *COMMAND* argument. The supported commands
are

**info** [*-l,--local*\ \|\ *-r,--rank=NODESET*]
   Dump a short-form summary of the total number of Machines, Cores,
   and Processing Units (PUs) available across all flux-brokers
   in the current instance. With *--ranks*, dump information for
   only the specified ranks. With *--local* dump local system information.

**lstopo**
   | Run ``lstopo(1)`` against the full hardware hierarchy configured in the
     current Flux instance. Extra ``OPTIONS`` are passed along to the system
     ``lstopo(1)``.
   | By default, **flux hwloc lstopo** generates console output.
     For graphical output, try: **flux hwloc lstopo --of graphical**.

**reload** [*-r,--rank=NODESET*] [*-v,--verbose] ['DIR*]
   Reload hwloc topology information, optionally loading hwloc XML files
   from ``DIR/<rank>.xml`` files. With *--rank* only reload XML on specified
   ranks. With *--verbose* this command runs with extra debugging and
   timing information.

**topology** [*-l,--local*\ \|\ *-r,--rank=NODESET*]
   Dump current aggregate topology XML for the current session to stdout.
   With *--rank* only dump aggregate topology for specified ranks. With
   *--local* dump topology XML for the local system.


NODESET FORMAT
==============

.. include:: NODESET.rst


RESOURCES
=========

Github: http://github.com/flux-framework


SEE ALSO
========

lstopo(1), hwloc: https://www.open-mpi.org/projects/hwloc/
