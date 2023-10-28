.. flux-help-include: true
.. flux-help-section: submission

=============
flux-alloc(1)
=============

SYNOPSIS
========

**flux** **alloc** [OPTIONS] *--nslots=N* [COMMAND...]

DESCRIPTION
===========

.. program:: flux alloc

**flux-alloc** runs a Flux subinstance with *COMMAND* as the initial program.
Once resources are allocated, *COMMAND* executes on the first node of the
allocation with any free arguments supplied as *COMMAND* arguments.  When
*COMMAND* exits, the Flux subinstance exits, resources are released to the
enclosing Flux instance, and **flux-alloc** returns.

If no *COMMAND* is specified, an interactive shell is spawned as the initial
program, and the subinstance runs until the shell is exited.

If the :option:`--bg` option is specified, the subinstance runs without an
initial program.  **flux-alloc** prints the jobid and returns as soon as the
subinstance is ready to accept jobs.  The subinstance runs until it exceeds
its time limit, is canceled, or is shut down with :man1:`flux-shutdown`.

Flux commands that are run from the subinstance (e.g. from the interactive
shell) refer to the subinstance. For example, :man1:`flux-run` would launch
work there.  A Flux command run from the subinstance can be forced to refer
to the enclosing instance by supplying the :option:`flux --parent` option.

Flux commands outside of the subinstance refer to their enclosing instance,
often a system instance. :man1:`flux-proxy` establishes a connection to a
running subinstance by jobid, then spawns a shell in which Flux commands
refer to the subinstance, for example

::

   $ flux alloc --bg -N 2 --queue=batch
   ƒM7Zq9AKHno
   $ flux proxy ƒM7Zq9AKHno
   $ flux mini run -n16 ./testprog
   ...
   $ flux shutdown
   ...
   $

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

RESOURCES
=========

Flux: http://flux-framework.org

SEE ALSO
========

:man1:`flux-run`, :man1:`flux-submit`, :man1:`flux-batch`,
:man1:`flux-bulksubmit`
