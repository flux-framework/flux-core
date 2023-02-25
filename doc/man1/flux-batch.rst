.. flux-help-include: true

=============
flux-batch(1)
=============


SYNOPSIS
========

**flux** **batch** [OPTIONS] *--nslots=N* SCRIPT...


DESCRIPTION
===========

The **flux-batch** command submits a script to run as the initial
program of a job running a new instance of Flux. The SCRIPT given on the
command line is assumed to be a file name unless the *--wrap* option is
used, in which case the free arguments are consumed as the script, with
``#!/bin/sh`` as the first line. If no SCRIPT is provided, then one will
be read from standard input. The script may contain submission directives
denoted by ``flux:`` or ``FLUX:`` as described in RFC 36, see
:ref:`submission_directives` below.

The SCRIPT given on the command line is assumed to be a file name, unless
the *--wrap* option used, and the script file is read and submitted along
with the job. If no SCRIPT is provided, then one will be read from *stdin*.

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
