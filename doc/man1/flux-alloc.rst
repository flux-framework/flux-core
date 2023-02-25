.. flux-help-include: true

=============
flux-alloc(1)
=============


SYNOPSIS
========

**flux** **alloc** [OPTIONS] *--nslots=N* [COMMAND...]

DESCRIPTION
===========

flux-alloc(1) interactively launches a command as the initial program of a
new Flux instance.  If no *COMMAND* is specified, a shell is spawned.

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
