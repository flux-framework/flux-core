.. flux-help-include: true
.. flux-help-section: submission

=============
flux-batch(1)
=============


SYNOPSIS
========

**flux** **batch** [OPTIONS] *--nslots=N* SCRIPT ...

**flux** **batch** [OPTIONS] *--nslots=N* --wrap COMMAND ...


DESCRIPTION
===========

**flux-batch** submits *SCRIPT* to run as the initial program of a Flux
subinstance.  *SCRIPT* refers to a file that is copied at the time of
submission.  Once resources are allocated, *SCRIPT* executes on the first
node of the allocation, with any remaining free arguments supplied as *SCRIPT*
arguments.  Once *SCRIPT* exits, the Flux subinstance exits and resources are
released to the enclosing Flux instance.

If there are no free arguments, the script is read from standard input.

If the *--wrap* option is used, the script is created by wrapping the free
arguments or standard input in a shell script prefixed with ``#!/bin/sh``.

If the job request is accepted, its jobid is printed on standard output and the
command returns.  The job runs when the Flux scheduler fulfills its resource
allocation request.  :man1:`flux-jobs` may be used to display the job status.

Flux commands that are run from the batch script refer to the subinstance.
For example, :man1:`flux-run` would launch work there.  A Flux command run
from the script can be forced to refer to the enclosing instance by supplying
the :man1:`flux` *--parent* option.

Flux commands outside of the batch script refer to their enclosing instance,
often a system instance.  :man1:`flux-proxy` establishes a connection to a
running subinstance by jobid, then spawns a shell in which Flux commands refer
to the subinstance.  For example:

::

   $ flux uptime
    07:48:42 run 2.1d,  owner flux,  depth 0,  size 8
   $ flux batch -N 2 --queue=batch mybatch.sh
   ƒM7Zq9AKHno
   $ flux proxy ƒM7Zq9AKHno
   $ flux uptime
    07:47:18 run 1.6m,  owner user42,  depth 1,  size 2
   $ flux top
   ...
   $ exit
   $

Other commands accept a jobid argument and establish the connection
automatically.  For example:

::

   $ flux batch -N 2 --queue=batch mybatch.sh
   ƒM7Zq9AKHno
   $ flux top ƒM7Zq9AKHno
   ...
   $

Batch scripts may contain submission directives denoted by ``flux:``
as described in RFC 36.  See :ref:`submission_directives` below.

The available OPTIONS are detailed below.

.. include:: common/submit-job-parameters.rst

.. include:: common/submit-standard-io.rst

.. include:: common/submit-constraints.rst

.. include:: common/submit-dependencies.rst

.. include:: common/submit-environment.rst

.. include:: common/submit-process-resource-limits.rst

.. include:: common/submit-exit-status.rst

.. include:: common/submit-other-options.rst

.. include:: common/submit-shell-options.rst

.. include:: common/submit-submission-directives.rst


RESOURCES
=========

Flux: http://flux-framework.org

SEE ALSO
========

:man1:`flux-submit`, :man1:`flux-run`, :man1:`flux-alloc`,
:man1:`flux-bulksubmit`
