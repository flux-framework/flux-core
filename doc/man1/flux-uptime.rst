.. flux-help-description: Tell how long Flux has been up and running
.. flux-help-section: instance

==============
flux-uptime(1)
==============


SYNOPSIS
========

**flux** **uptime**


DESCRIPTION
===========

The :program:`flux uptime` command displays the following information about the
current Flux instance, on one or two lines:

- The current wall clock time.

- The broker state.  See BROKER STATES.

- The elapsed time the Flux instance has been running, in RFC 23 Flux Standard
  Duration format.  If the local broker is not in **run** state, the elapsed
  time in the current state is reported instead.

- The Flux instance owner.  On a system instance, this is probably the
  ``flux`` user.

- The Flux instance depth.  If the Flux instance was not launched as a job
  within another Flux instance, the depth is zero.

- The instance size.  This is the total number of brokers, which is usually
  also the number of nodes.

- The number of drained nodes, if greater than zero.  Drained nodes are
  not eligible to run new jobs, although they may be online, and may currently
  be running a job.

- The number of offline nodes, if greater than zero.  A node is offline if
  its broker is not connected to the instance overlay network.

- A notice if job submission is disabled on all queues.

- A notice if scheduling is disabled.


BROKER STATES
=============

join
   The local broker is trying to connect to its overlay network parent,
   or is waiting for the parent to complete initialization and reach
   **quorum** state.

init
   The local broker is waiting for the ``rc1`` script to complete locally.

quorum
   All brokers are waiting for a configured number of brokers to reach
   **quorum** state.  The default quorum is the instance size.  A Flux
   system instance typically defines the quorum size to 1.

run
   Flux is fully up and running.

cleanup
   Cleanup scripts are executing.  This state appears on the rank 0 broker only.

shutdown
   The local broker is waiting for its overlay network children to finalize
   and disconnect.

finalize
   The local broker is waiting for the ``rc3`` script to complete locally.


RESOURCES
=========

.. include:: common/resources.rst

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man1:`flux-resource`, :man1:`flux-getattr`, :man7:`flux-broker-attributes`
