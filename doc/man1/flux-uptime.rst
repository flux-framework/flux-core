.. flux-help-description: Tell how long Flux has been up and running

==============
flux-uptime(1)
==============


SYNOPSIS
========

**flux** **uptime**


DESCRIPTION
===========

The ``flux-uptime`` displays the following information about the
current Flux instance, on one or two lines:

- The current wall clock time.

- The elapsed time the Flux instance has been running, in RFC 23 Flux Standard
  Duration format.  This is derived from the ``broker.starttime`` attribute
  on the rank 0 broker.

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

- A notice if job submission is disabled.

- A notice if scheduling is disabled.


RESOURCES
=========

Flux: http://flux-framework.org

RFC 23: Flux Standard Duration: https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/spec_23.html


SEE ALSO
========

:man1:`flux-resource`, :man1:`flux-getattr`, :man7:`flux-broker-attributes`
