***************
Troubleshooting
***************

Overlay network
===============

The tree-based overlay network interconnects brokers of the system instance.
The current status of the overlay subtree at any rank can be shown with:

.. code-block:: console

 $ flux overlay status -r RANK

The possible status values are:

**Full**
  Node is online and no children are in partial, offline, degraded, or lost
  state.

**Partial**
  Node is online, and some children are in partial or offline state; no
  children are in degraded or lost state.

**Degraded**
  Node is online, and some children are in degraded or lost state.

**Lost**
  Node has gone missing, from the parent perspective.

**Offline**
  Node has not yet joined the instance, or has been cleanly shut down.

Note that the RANK argument is where the request will be sent, not necessarily
the rank whose status is of interest.  Parents track the status of their
children, so a good approach when something is wrong to start with rank 0
(the default).  The following options can be used to ask rank 0 for a detailed
listing:

.. code-block:: console

 $ flux overlay status
 0 fluke62: degraded
 ├─ 1 fluke63: full
 │  ├─ 3 fluke65: full
 │  │  ├─ 7 fluke70: full
 │  │  └─ 8 fluke71: full
 │  └─ 4 fluke67: full
 │     ├─ 9 fluke72: full
 │     └─ 10 fluke73: full
 └─ 2 fluke64: degraded
    ├─ 5 fluke68: full
    │  ├─ 11 fluke74: full
    │  └─ 12 fluke75: full
    └─ 6 fluke69: degraded
       ├─ 13 fluke76: full
       └─ 14 fluke77: lost

To determine if a broker is reachable from the current rank, use:

.. code-block:: console

 $ flux ping RANK

A broker that is not responding but is not shown as lost or offline
by ``flux overlay status`` may be forcibly detached from the overlay
network with:

.. code-block:: console

 $ flux overlay disconnect RANK

However, before doing that, it may be useful to see if a broker acting
as a router to that node is actually the problem.  The overlay parent
of RANK may be listed with

.. code-block:: console

 $ flux overlay parentof RANK

Using ``flux ping`` and ``flux overlay parentof`` iteratively, one should
be able to isolate the problem rank.

See also :man1:`flux-overlay`, :man1:`flux-ping`.

Systemd journal
===============

Flux brokers log information to standard error, which is normally captured
by the systemd journal.  It may be useful to look at this log when diagnosing
a problem on a particular node:

.. code-block:: console

 $ journalctl -u flux
 Sep 14 09:53:12 sun1 systemd[1]: Starting Flux message broker...
 Sep 14 09:53:12 sun1 systemd[1]: Started Flux message broker.
 Sep 14 09:53:12 sun1 flux[23182]: broker.info[2]: start: none->join 0.0162958s
 Sep 14 09:53:54 sun1 flux[23182]: broker.info[2]: parent-ready: join->init 41.8603s
 Sep 14 09:53:54 sun1 flux[23182]: broker.info[2]: rc1.0: running /etc/flux/rc1.d/01-enclosing-instance
 Sep 14 09:53:54 sun1 flux[23182]: broker.info[2]: rc1.0: /bin/sh -c /etc/flux/rc1 Exited (rc=0) 0.4s
 Sep 14 09:53:54 sun1 flux[23182]: broker.info[2]: rc1-success: init->quorum 0.414207s
 Sep 14 09:53:54 sun1 flux[23182]: broker.info[2]: quorum-full: quorum->run 9.3847e-05s

Broker log buffer
=================

The rank 0 broker accumulates log information for the full instance in a
circular buffer.  For some problems, it may be useful to view this log:

.. code-block:: console

 $ sudo flux dmesg -H |tail

 [May02 14:51] sched-fluxion-qmanager[0]: feasibility_request_cb: feasibility succeeded
 [  +0.039371] sched-fluxion-qmanager[0]: alloc success (queue=debug id=184120855100391424)
 [  +0.816587] sched-fluxion-qmanager[0]: feasibility_request_cb: feasibility succeeded
 [  +0.857458] sched-fluxion-qmanager[0]: alloc success (queue=debug id=184120868807376896)
 [  +1.364430] sched-fluxion-qmanager[0]: feasibility_request_cb: feasibility succeeded
 [  +6.361275] job-ingest[0]: job-frobnicator[0]: inactivity timeout
 [  +6.367837] job-ingest[0]: job-validator[0]: inactivity timeout
 [ +24.778929] job-exec[0]: exec aborted: id=184120855100391424
 [ +24.779019] job-exec[0]: exec_kill: 184120855100391424: signal 15
 [ +24.779557] job-exec[0]: exec aborted: id=184120868807376896
 [ +24.779632] job-exec[0]: exec_kill: 184120868807376896: signal 15
 [ +24.779910] sched-fluxion-qmanager[0]: alloc canceled (id=184120878001291264 queue=debug)
 [ +25.155578] job-list[0]: purged 1 inactive jobs
 [ +25.162650] job-manager[0]: purged 1 inactive jobs
 [ +25.512050] sched-fluxion-qmanager[0]: free succeeded (queue=debug id=184120855100391424)
 [ +25.647542] sched-fluxion-qmanager[0]: free succeeded (queue=debug id=184120868807376896)
 [ +27.155103] job-list[0]: purged 2 inactive jobs
 [ +27.159820] job-manager[0]: purged 2 inactive jobs
