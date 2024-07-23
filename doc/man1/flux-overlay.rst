.. flux-help-description: Show flux overlay network status
.. flux-help-section: instance

===============
flux-overlay(1)
===============


SYNOPSIS
========

| **flux** **overlay** **status** [*-v*] [*--timeout=FSD*] [*--rank=N*]
| **flux** **overlay** **errors** [*--timeout=FSD*]
| **flux** **overlay** **lookup** *target*
| **flux** **overlay** **parentof** *rank*
| **flux** **overlay** **disconnect** [*--parent=RANK*] *target*


DESCRIPTION
===========

.. program:: flux overlay

:program:`flux overlay` is a utility for the Flux tree based overlay network.

COMMANDS
========

status
------

.. program:: flux overlay status

:program:`flux overlay status` reports the current status of the tree based
overlay network.  The possible status values are shown in `SUBTREE STATUS`_
below.

.. option:: -r, --rank=[RANK]

   Check health of sub-tree rooted at NODEID (default 0).

.. option:: -v, --verbose=[LEVEL]

   Increase reporting detail: 1=show time since current state was entered,
   2=show round-trip RPC times.

.. option:: -t, --timeout=FSD

   Set RPC timeout, 0=disable (default 0.5s)

.. option:: --summary

   Show only the root sub-tree status.

.. option:: --down

   Show only the partial/degraded sub-trees.

.. option:: --no-pretty

   Do not indent entries and use line drawing characters to show overlay
   tree structure

.. option:: --no-ghost

   Do not fill in presumed state of nodes that are inaccessible behind
   offline/lost overlay parents.

.. option:: -L, --color=WHEN

   Colorize output when supported; WHEN can be 'always' (default if omitted),
   'never', or 'auto' (default).

.. option:: -H, --highlight=TARGET

   Highlight one or more targets and their ancestors.

.. option:: -w, --wait=STATE

   Wait until sub-tree enters *STATE* before reporting (full, partial, offline,
   degraded, lost).

errors
------

.. program:: flux overlay errors

:program:`flux overlay errors` summarizes any errors recorded for lost nodes.
The output consists of one line per unique error with a hostlist prefix.

.. option:: -t, --timeout=FSD

   Set RPC timeout, 0=disable (default 0.5s)

lookup
------

.. program:: flux overlay lookup

Translate a hostlist *target* to a rank idset or a rank idset *target* to
hostlist.

parentof
--------

.. program:: flux overlay parentof

Show the parent of *rank*.

disconnect
----------

.. program:: flux overlay disconnect

Disconnect a subtree rooted at *target* (hostname or rank).

.. option:: -r, --parent=NODEID

  Set parent rank to *NODEID*.  By default, the parent is determined from
  the topology.


EXAMPLES
========

By default, :program:`flux overlay status` shows the status of the full
Flux instance in graphical form, e.g.

::

  $ flux overlay status
  0 test0: partial
  ├─ 1 test1: offline
  ├─ 2 test2: full
  ├─ 3 test3: full
  ├─ 4 test4: full
  ├─ 5 test5: offline
  ├─ 6 test6: full
  └─ 7 test7: offline

The time in the current state is reported if ``-v`` is added, e.g.

::

  $ flux overlay status -v
  0 test0: partial for 2.55448m
  ├─ 1 test1: offline for 12.7273h
  ├─ 2 test2: full for 2.55484m
  ├─ 3 test3: full for 18.2725h
  ├─ 4 test4: full for 18.2777h
  ├─ 5 test5: offline for 12.7273h
  ├─ 6 test6: full for 18.2784h
  └─ 7 test7: offline for 12.7273h

Round trip RPC times are shown with ``-vv``, e.g.

::

  0 test0: partial for 4.15692m (2.982 ms)
  ├─ 1 test1: offline for 12.754h
  ├─ 2 test2: full for 4.15755m (2.161 ms)
  ├─ 3 test3: full for 18.2992h (2.332 ms)
  ├─ 4 test4: full for 18.3045h (2.182 ms)
  ├─ 5 test5: offline for 12.754h
  ├─ 6 test6: full for 18.3052h (2.131 ms)
  └─ 7 test7: offline for 12.754h



::

At times an error summary for *lost* nodes may be useful, e.g.

::

  $ flux overlay errors
  test[2,5]: lost connection
  test[6-7]: lost parent

A broker that is not responding but is not shown as *lost* or *offline* may
be forcibly disconnected from the overlay network with

::

  $ flux overlay disconnect 2
  flux-overlay: asking test0 (rank 0) to disconnect child test2 (rank 2)

However, before doing that it may be useful to see if a broker acting as a
router to that node is actually the problem.  The parent of a broker rank may
be listed with

::

  $ flux overlay parentof 2
  0

Finally, translation between hostnames and broker ranks is accomplished with

::

  $ flux overlay lookup 2
  test2
  $ flux overlay lookup test2
  2


SUBTREE STATUS
==============

The possible overlay subtree status values are:

full
    Node is online and no children are in *partial*, *offline*, *degraded*, or
    *lost* state.

partial
    Node is online, and some children are in *partial* or *offline* state; no
    children are in *degraded* or *lost* state.

degraded
    Node is online, and some children are in *degraded* or *lost* state.

lost
    Node has gone missing, from the parent perspective.

offline
    Node has not yet joined the instance, or has been cleanly shut down.


RESOURCES
=========

.. include:: common/resources.rst

FLUX RFC
========

:doc:`rfc:spec_3`


SEE ALSO
========

:man1:`flux-ping`
