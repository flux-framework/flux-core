.. flux-help-include: true
.. flux-help-section: instance

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
- If a node is drained and offline, then "drained*" will be displayed.

Some further background on resource service operation may be found in the
RESOURCE INVENTORY section below.


COMMANDS
========

**list** [-n] [-o FORMAT] [-s STATE,...] [-i TARGETS]
   Show scheduler view of resources.

   With *-s,--states=STATE,...*, the set of resource states is restricted
   to a list of provided states. Valid states include "up", "down",
   "allocated", "free", and "all". Note that the scheduler represents
   offline, excluded, and drained resources as "down" due to the simplified
   interface with the resource service defined by RFC 27.

   With *-i, --include=TARGETS*, the results are filtered to only include
   resources matching **TARGETS**, which may be specified either as an idset
   of broker ranks or list of hosts in hostlist form. It is not an error to
   specify ranks or hosts which do not exist, the result will be filtered
   to include only those ranks or hosts that are present in *TARGETS*.

   The *-o,--format=FORMAT* option may be used to customize the output
   format (See OUTPUT FORMAT section below).

   The *-n,--no-header* option suppresses header from output,

**info** [-s STATE,...] [-i TARGETS]
   Show a brief, single line summary of scheduler view of resources.

   With *-s, --states=STATE,...*, limit the output to specified resource
   states as with ``flux resource list``. By default, the *STATE* reported
   by ``flux resource info`` is "all".

   With *-i, --include=TARGETS*, the results are filtered to only include
   resources matching **TARGETS**, which may be specified either as an idset
   of broker ranks or list of hosts in hostlist form. It is not an error to
   specify ranks or hosts which do not exist, the result will be filtered
   to include only those ranks or hosts that are present in *TARGETS*.

**status**  [-n] [-o FORMAT] [-s STATE,...] [-i TARGETS] [--skip-empty]
   Show system view of resources. This command queries both the resource
   service and scheduler to identify resources that are available,
   excluded by configuration, or administratively drained or draining.

   The **status** command displays a line of output for each set of
   resources that share a state and online/offline state. The possible
   states are "avail" (available for scheduling when up), "exclude"
   (excluded by configuration), "draining" (drained but still allocated),
   or "drained".

   With *-s,--states=STATE,...*, the set of resource states is restricted
   to a list of provided states or offline/online status. With "online" or
   "offline", only nodes with the provided status will be displayed. Other
   valid states include "avail", "exclude", "draining", "drained", and "all".
   The special "drain" state is shorthand for "drained,draining".

   With *-i, --include=TARGETS*, the results are filtered to only include
   resources matching **TARGETS**, which may be specified either as an idset
   of broker ranks or list of hosts in hostlist form. It is not an error to
   specify ranks or hosts which do not exist, the result will be filtered
   to include only those ranks or hosts that are present in *TARGETS*.

   The *-o,--format=FORMAT* option customizes output formatting (See the
   OUTPUT FORMAT section below for details).

   With *-n,--no-header* the output header is suppressed.

   Normally, ``flux resource status`` skips lines with no resources,
   unless the ``-s, --states`` option is used. Suppression of empty lines
   can may be forced with the ``--skip-empty`` option.

**drain** [-n] [-o FORMAT] [-i TARGETS] [-f] [-u] [targets] [reason ...]
   If specified without arguments, list drained nodes. In this mode,
   *-n,--no-header* suppresses header from output and *-o,--format=FORMAT*
   customizes output formatting (see below).  The *targets* argument is an
   IDSET or HOSTLIST specifying nodes to drain.  Any remaining arguments
   are assumed to be a reason to be recorded with the drain event.

   With *-i, --include=TARGETS*, **drain** output is filtered to only include
   resources matching **TARGETS**, which may be specified either as an idset
   of broker ranks or list of hosts in hostlist form. It is not an error to
   specify ranks or hosts which do not exist, the result will be filtered
   to include only those ranks or hosts that are present in *TARGETS*.

   By default, **flux resource drain** will fail if any of the *targets*
   are already drained. To change this behavior, use either of the
   *-f, --force* or *-u, --update* options. With *--force*, the *reason* for
   all existing drained targets is overwritten. If *--force* is specified
   twice, then the timestamp is also overwritten. With *--update*,
   only those ranks that are not already drained or do not have a *reason* set
   have their *reason* updated.

   Resources cannot be both excluded and drained, so **flux resource drain**
   will also fail if any *targets* are currently excluded by configuration.
   There is no option to force an excluded node into the drain state.

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
string format syntax or a defined format by name. For a list of built-in and
configured formats use ``-o help``.  An alternate default format can be set via
the FLUX_RESOURCE_STATUS_FORMAT_DEFAULT, FLUX_RESOURCE_DRAIN_FORMAT_DEFAULT, and
FLUX_RESOURCE_LIST_FORMAT_DEFAULT environment variables (for ``flux resource
status``, ``flux resource drain``, and ``flux resource list`` respectively).  A
configuration snippet for an existing named format may be generated with
``--format=get-config=NAME``.  See :man1:`flux-jobs` *OUTPUT FORMAT* section for
a detailed description of this syntax.

Resources are combined into a single line of output when possible depending on
the supplied output format.  Resource counts are not included in the
determination of uniqueness.  Therefore, certain output formats will alter the
number of lines of output.  For example:

::

   $ flux resource list -no {nnodes}

Would simply output a single of output containing the total number of nodes.
The actual state of the nodes would not matter in the output.

The following field names can be specified for the **status** and **drain**
subcommands:

**state**
   State of node(s): "avail", "exclude", "drain", "draining", "drained". If
   the set of resources is offline, an asterisk suffix is appended to the
   state, e.g. "avail*".

**statex**
   Like **state**, but exclude the asterisk for offline resources.

**status**
   Current online/offline status of nodes(s): "online", "offline"

**up**
   Displays a *✔* if the node is online, or *✗* if offline. An ascii *y*
   or *n* may be used instead with **up.ascii**.

**nnodes**
   number of nodes

**ranks**
   ranks of nodes

**nodelist**
   node names

**timestamp**
   If node(s) in drain/draining/drained state, timestamp of node(s)
   set to drain.

**reason**
   If node(s) in drain/draining/drained state, reason node(s) set to
   drain.

The following field names can be specified for the **list** subcommand:

**state**
   State of node(s): "up", "down", "allocated", "free", "all"

**queue**
   queue(s) associated with resources.

**properties**
   Properties associated with resources.

**propertiesx**
   Properties associated with resources, but with queue names removed.

**nnodes**
   number of nodes

**ncores**
   number of cores

**ngpus**
   number of gpus

**ranks**
   ranks of nodes

**nodelist**
   node names

**rlist**
   Short form string of all resources.


CONFIGURATION
=============

Similar to :man1:`flux-jobs`, the ``flux-resource`` command supports loading
a set of config files for customizing utility output formats. Currently
this can be used to register named format strings for the ``status``,
``list``, and ``drain`` subcommands.

Configuration for each ``flux-resource`` subcommand is defined in a separate
table, so to add a new format ``myformat`` for ``flux resource list``,
the following config file could be used::

  # $HOME/.config/flux/flux-resource.toml
  [list.formats.myformat]
  description = "My flux resource list format"
  format = "{state} {nodelist}"

See :man1:`flux-jobs` *CONFIGURATION* section for more information about the
order of precedence for loading these config files.

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
