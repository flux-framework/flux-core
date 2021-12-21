.. flux-help-command: hwloc
.. flux-help-description: Control/query resource-hwloc service

=============
flux-hwloc(1)
=============


SYNOPSIS
========

**flux** **hwloc** **info** [*OPTIONS*]

**flux** **hwloc** **topology** [*OPTIONS*]


DESCRIPTION
===========

The **flux-hwloc** utility queries :linux:man7:`hwloc` topology information for
an instance by gathering XML from the core resource module.

COMMANDS
========

**flux hwloc** requires a *COMMAND* argument. The supported commands
are

**info** [*-l,--local*\ \|\ *-r,--rank=NODESET*]
   Dump a short-form summary of the total number of Machines, Cores,
   and Processing Units (PUs) available across all flux-brokers
   in the current instance. With *--ranks*, dump information for
   only the specified ranks. With *--local* dump local system information.

**topology** [*-l,--local*\ \|\ *-r,--rank=NODESET*]
   Dump current aggregate topology XML for the current session to stdout.
   With *--rank* only dump aggregate topology for specified ranks. With
   *--local* dump topology XML for the local system. With hwloc < 2.0,
   this command will dump a custom topology with multiple machines when
   the aggregate contains multiple ranks. This is not possible with hwloc
   2.0 because multiple Machine objects in a topology is no longer supported,
   and therefore the XML for each rank will be printed separately.
    


NODESET FORMAT
==============

.. include:: NODESET.rst


EXAMPLES
========

When using HWLOC < 2.0 only, the output of ``flux hwloc topology``
may be piped to other :linux:man7:`hwloc` commands such as
:linux:man1:`lstopo` or :linux:man1:`hwloc-info`, e.g.

::

    $ flux hwloc topology | lstopo-no-graphics --if xml -i -
    System (31GB total)
      Machine L#0 (7976MB) + Package L#0
        Core L#0 + PU L#0 (P#0)
        Core L#1 + PU L#1 (P#1)
        Core L#2 + PU L#2 (P#2)
        Core L#3 + PU L#3 (P#3)
      Machine L#1 (7976MB) + Package L#1
        Core L#4 + PU L#4 (P#0)
        Core L#5 + PU L#5 (P#1)
        Core L#6 + PU L#6 (P#2)
        Core L#7 + PU L#7 (P#3)
      Machine L#2 (7976MB) + Package L#2
        Core L#8 + PU L#8 (P#0)
        Core L#9 + PU L#9 (P#1)
        Core L#10 + PU L#10 (P#2)
        Core L#11 + PU L#11 (P#3)
      Machine L#3 (7976MB) + Package L#3
        Core L#12 + PU L#12 (P#0)
        Core L#13 + PU L#13 (P#1)
        Core L#14 + PU L#14 (P#2)
        Core L#15 + PU L#15 (P#3)

RESOURCES
=========

Flux: http://flux-framework.org

hwloc: https://www.open-mpi.org/projects/hwloc/


SEE ALSO
========

:linux:man1:`lstopo`
