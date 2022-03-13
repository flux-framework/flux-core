.. flux-help-include: true

================
flux-resource(1)
================


SYNOPSIS
========

**flux** **resource** *COMMAND* [*OPTIONS*]

DESCRIPTION
===========

flux-resource(1) lists and manipulates Flux resources.  The resource inventory
is maintained and monitored by the resource service.  The scheduler acquires
a subset of resources from the resource service to allocate to jobs, and relies
on the resource service to inform it of status changes that affect the
usability of resources by jobs as described in RFC 27.

The flux-resource(1) **list** subcommand queries the scheduler for its view
of resources, including allocated/free status.

The other flux-resource(1) subcommands operate on the resource service and
are primarily of interest to system administrators of a Flux system instance.
For example, they can show whether or not a node is booted, and may be used to
administratively drain and undrain nodes.

A few notes on drained nodes:

- While a node is drained, the scheduler will not allocate it to jobs.
- The act of draining a node does not affect running jobs.
- When an instance is restarted, drained nodes remain drained.
- The scheduler may determine that a job request is *feasible* if the total
  resource set, including drained nodes, would allow it to run.
- In ``flux resource status`` and ``flux resource drain``, the drain state
  of a node will be presented as "drained" if the node has no job allocations,
  and "draining" if there are still jobs running on the node.

Some further background on resource service operation may be found in the
RESOURCE INVENTORY section below.


COMMANDS
========

**list** [-v] [-n] [-o FORMAT] [-s STATE,...]
   Show scheduler view of resources.  One or more *-v,--verbose* options
   increase output verbosity.  *-n,--no-header* suppresses header from output.
   *-o,--format=FORMAT*, customizes output formatting (see below).
   *-s,--states=STATE,...* limits output to specified resource states, where
   valid states are "up", "down", "allocated", "free", and "all".  Note that
   the scheduler represents "offline", "exclude", and "drain" resource states
   as "down" due to its simplified interface with the resource service defined
   by RFC 27.

**status**  [-v] [-n] [-o FORMAT] [-s STATE,...]
   Show system view of resources.  One or more *-v,--verbose* options
   increase output verbosity.  *-n,--no-header* suppresses header from output.
   *-o,--format=FORMAT*, customizes output formatting (see below).
   *-s,--states=STATE,...* limits output to specified resource states, where
   valid states are "online", "offline", "avail", "exclude", "draining",
   "drained", and "all". The special "drain" state is also supported, and
   selects both draining and drained resources.

**drain** [-f] [-u] [targets] [reason ...]
   If specified without arguments, list drained nodes.  The *targets* argument
   is an IDSET or HOSTLIST specifying nodes to drain.  Any remaining arguments
   are assumed to be a reason to be recorded with the drain event.

   By default, **flux resource drain** will fail if any of the *targets*
   are already drained. To change this behavior, use either of the
   *-f, --force* or *-u, --update* options. With *--force*, the *reason* for
   all existing drained targets is overwritten, while with *--update*,
   only those ranks that are not already drained or do not have a *reason* set
   have their *reason* updated.

   This command, when run with arguments, is restricted to the Flux instance
   owner.

**undrain** targets
   The *targets* argument is an IDSET or HOSTLIST specifying nodes to undrain.
   This command is restricted to the Flux instance owner.

**reload** [-x] [-f] PATH
   Reload the resource inventory from a file in RFC 20 format, or if the
   *-x,--xml* option, a directory of hwloc ``<rank>.xml`` files.  If
   *-f,--force*, resources may contain invalid ranks.  This command is
   primarily used in test.


OUTPUT FORMAT
=============

The *--format* option can be used to specify an output format using Python's
string format syntax.  See :man1:`flux-jobs` for a detailed description of
this syntax.


RESOURCE INVENTORY
==================

The Flux instance's inventory of resources is managed by the resource service,
which determines the set of available resources through one of three
mechanisms:

configuration
   Resources are read from a config file in RFC 20 (R version 1) format.
   This mechanism is typically used in a system instance of Flux.

enclosing instance
   Resources are assigned by the enclosing Flux instance.  The assigned
   resources are read from the job's ``R`` key in the enclosing instance KVS.

dynamic discovery
   Resources are aggregated from the set of resources reported by hwloc
   on each broker.

Once the inventory has been determined, it is stored the KVS ``resource.R``
key, in RFC 20 (R version 1) format.

Events that affect the availability of resources are posted to the KVS
*resource.eventlog*.  Such events include:

resource-define
   The resource inventory is defined with an initial set of drained, online,
   and excluded nodes.

drain
   One or more nodes are administratively removed from scheduling.

undrain
   One or more nodes are no longer drained.

offline
   One or more nodes are removed from scheduling due to unavailability,
   e.g. node was shutdown or crashed.

online
   One or more nodes are no longer offline.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 20: Resource Set Specification Version 1: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_20.html

RFC 27: Flux Resource Allocation Protocol Version 1: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_27.html
