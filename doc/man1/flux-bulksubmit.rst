.. flux-help-include: true
.. flux-help-section: submission

==================
flux-bulksubmit(1)
==================


SYNOPSIS
========

**flux** **bulksubmit** [OPTIONS] COMMAND...


DESCRIPTION
===========

.. program:: flux bulksubmit

:program:`flux bulksubmit` allows rapid bulk submission of jobs using
an interface similar to GNU parallel or ``xargs``. The command takes
inputs on stdin or the command line (separated by ``:::``), and submits
the supplied command template and options as one job per input combination.

The replacement is done using Python's ``string.format()``, which is
supplied a list of inputs on each iteration. Therefore, in the common case
of a single input list, ``{}`` will work as the substitution string, e.g.::

    $ seq 1 4 | flux bulksubmit echo {}
    bulksubmit: submit echo 1
    bulksubmit: submit echo 2
    bulksubmit: submit echo 3
    bulksubmit: submit echo 4

With :option:`--dry-run` :program:`flux bulksubmit` will print the args and
command which would have been submitted, but will not perform any job
submission.

The :program:`flux bulksubmit` command can also take input lists on the command
line.  The inputs are separated from each other and the command  with the
special delimiter ``:::``::

    $ flux bulksubmit echo {} ::: 1 2 3 4
    bulksubmit: submit echo 1
    bulksubmit: submit echo 2
    bulksubmit: submit echo 3
    bulksubmit: submit echo 4

Multiple inputs are combined, in which case each input is passed as a
positional parameter to the underlying ``format()``, so should be accessed
by index::

    $ flux bulksubmit --dry-run echo {1} {0} ::: 1 2 ::: 3 4
    bulksubmit: submit echo 3 1
    bulksubmit: submit echo 4 1
    bulksubmit: submit echo 3 2
    bulksubmit: submit echo 4 2

If the generation of all combinations of an  input list with other inputs is not
desired, the special input delimited ``:::+`` may be used to "link" the input,
so that only one argument from this source will be used per other input,
e.g.::

    $ flux bulksubmit --dry-run echo {0} {1} ::: 1 2 :::+ 3 4
    bulksubmit: submit 1 3
    bulksubmit: submit 2 4

The linked input will be cycled through if it is shorter than other inputs.

An input list can be read from a file with ``::::``::

    $ seq 0 3 >inputs
    $ flux bulksubmit --dry-run :::: inputs
    bulksubmit: submit 0
    bulksubmit: submit 1
    bulksubmit: submit 2
    bulksubmit: submit 3

If the filename is ``-`` then ``stdin`` will be used. This is useful
for including ``stdin`` when reading other inputs.

The delimiter ``::::+`` indicates that the next file is to be linked to
the inputs instead of combined with them, as with ``:::+``.

There are several predefined attributes for input substitution.
These include:

 - ``{.%}`` returns the input string with any extension removed.
 - ``{./}`` returns the basename of the input string.
 - ``{./%}`` returns the basename of the input string with any
   extension removed.
 - ``{.//}`` returns the dirname of the input string
 - ``{seq}`` returns the input sequence number (0 origin)
 - ``{seq1}`` returns the input sequence number (1 origin)
 - ``{cc}`` returns the current ``id`` from use of :option:`--cc` or
   :option:`--bcc`.  Note that replacement of ``{cc}`` is done in a second
   pass, since the :option:`--cc` option argument may itself be replaced in
   the first substitution pass. If :option:`--cc`/:option:`--bcc` were not
   used, then ``{cc}`` is replaced with an empty string. This is the only
   substitution supported with :man1:`flux-submit`.

Note that besides ``{seq}``, ``{seq1}``, and ``{cc}`` these attributes
can also take the input index, e.g. ``{0.%}`` or ``{1.//}``, when multiple
inputs are used.

Additional attributes may be defined with the :option:`--define` option, e.g.::

    $ flux bulksubmit --dry-run --define=p2='2**int(x)' -n {.p2} hostname \
       ::: $(seq 0 4)
    bulksubmit: submit -n1 hostname
    bulksubmit: submit -n2 hostname
    bulksubmit: submit -n4 hostname
    bulksubmit: submit -n8 hostname
    bulksubmit: submit -n16 hostname

The input string being indexed is passed to defined attributes via the
local ``x`` as seen above.

The available OPTIONS are detailed below.

JOB PARAMETERS
==============

These commands accept only the simplest parameters for expressing
the size of the parallel program and the geometry of its task slots:

Common resource options
-----------------------

These commands take the following common resource allocation options:

.. include:: common/job-param-common.rst

Per-task options
----------------

:man1:`flux-run`, :man1:`flux-submit` and :man1:`flux-bulksubmit` take two
sets of mutually exclusive options to specify the size of the job request.
The most common form uses the total number of tasks to run along with
the amount of resources required per task to specify the resources for
the entire job:

.. include:: common/job-param-pertask.rst

Per-resource options
--------------------

The second set of options allows an amount of resources to be specified
with the number of tasks per core or node set on the command line. It is
an error to specify any of these options when using any per-task option
listed above:

.. include:: common/job-param-perres.rst

Additional job options
----------------------

These commands also take following job parameters:

.. include:: common/job-param-additional.rst

STANDARD I/O
============

By default, task stdout and stderr streams are redirected to the
KVS, where they may be accessed with the ``flux job attach`` command.

In addition, :man1:`flux-run` processes standard I/O in real time,
emitting the job's I/O to its stdout and stderr.

.. include:: common/job-standard-io.rst

CONSTRAINTS
===========

.. include:: common/job-constraints.rst

DEPENDENCIES
============

.. include:: common/job-dependencies.rst

ENVIRONMENT
===========

By default, these commands duplicate the current environment when submitting
jobs. However, a set of environment manipulation options are provided to
give fine control over the requested environment submitted with the job.

.. include:: common/job-environment.rst

ENV RULES
---------

.. include:: common/job-env-rules.rst

PROCESS RESOURCE LIMITS
=======================

By default these commands propagate some common resource limits (as described
in :linux:man2:`getrlimit`) to the job by setting the ``rlimit`` job shell
option in jobspec.  The set of resource limits propagated can be controlled
via the :option:`--rlimit=RULE` option:

.. include:: common/job-process-resource-limits.rst

EXIT STATUS
===========

The job exit status, normally the largest task exit status, is stored
in the KVS. If one or more tasks are terminated with a signal,
the job exit status is 128+signo.

The ``flux-job attach`` command exits with the job exit status.

In addition, :man1:`flux-run` runs until the job completes and exits
with the job exit status.

OTHER OPTIONS
=============

.. include:: common/job-other-options.rst

.. include:: common/job-other-run.rst

SHELL OPTIONS
=============

Some options that affect the parallel runtime environment are provided by the
Flux shell.  These options are described in detail in the
:ref:`SHELL OPTIONS <flux_shell_options>` section of :man1:`flux-shell`.
A list of the most commonly needed options follows.

Usage: :option:`flux bulksubmit -o NAME[=ARG]`.

.. make :option: references in the included table x-ref to flux-shell(1)
.. program:: flux shell
.. include:: common/job-shell-options.rst
.. program:: flux bulksubmit

MUSTACHE TEMPLATES
==================

.. include:: common/job-mustache-templates.rst

RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-run`, :man1:`flux-submit`, :man1:`flux-alloc`,
:man1:`flux-batch`, :man7:`flux-environment`

