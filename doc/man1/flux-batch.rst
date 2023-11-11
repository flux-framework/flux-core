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

.. program:: flux batch

:program:`flux-batch` submits *SCRIPT* to run as the initial program of a Flux
subinstance.  *SCRIPT* refers to a file that is copied at the time of
submission.  Once resources are allocated, *SCRIPT* executes on the first
node of the allocation, with any remaining free arguments supplied as *SCRIPT*
arguments.  Once *SCRIPT* exits, the Flux subinstance exits and resources are
released to the enclosing Flux instance.

If there are no free arguments, the script is read from standard input.

If the :option:`--wrap` option is used, the script is created by wrapping the
free arguments or standard input in a shell script prefixed with ``#!/bin/sh``.

If the job request is accepted, its jobid is printed on standard output and the
command returns.  The job runs when the Flux scheduler fulfills its resource
allocation request.  :man1:`flux-jobs` may be used to display the job status.

Flux commands that are run from the batch script refer to the subinstance.
For example, :man1:`flux-run` would launch work there.  A Flux command run
from the script can be forced to refer to the enclosing instance by supplying
the :option:`flux --parent` option.

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
as described in RFC 36.  See `SUBMISSION DIRECTIVES`_ below.

The available OPTIONS are detailed below.

.. include:: common/job-parameters.rst

.. include:: common/job-standard-io.rst

.. include:: common/job-constraints.rst

.. include:: common/job-dependencies.rst

.. include:: common/job-environment.rst

.. include:: common/job-env-rules.rst

.. include:: common/job-process-resource-limits.rst

.. include:: common/job-exit-status.rst

.. include:: common/job-other-options.rst

.. include:: common/job-shell-options.rst

SUBMISSION DIRECTIVES
=====================

The :program:`flux batch` command supports submission directives
mixed within the submission script. The submission directive specification
is fully detailed in RFC 36, but is summarized here for convenience:

 * A submission directive is indicated by a line that starts with
   a prefix of non-alphanumeric characters followed by a tag ``FLUX:`` or
   ``flux:``. The prefix plus tag is called the *directive sentinel*. E.g.,
   in the example below the sentinel is ``# flux:``: ::

     #!/bin/sh
     # flux: -N4 -n16
     flux run -n16 hostname

 * All directives in a file must use the same sentinel pattern, otherwise
   an error will be raised.
 * Directives must be grouped together - it is an error to include a
   directive after any non-blank line that doesn't start with the common
   prefix.
 * The directive starts after the sentinel to the end of the line.
 * The ``#`` character is supported as a comment character in directives.
 * UNIX shell quoting is supported in directives.
 * Triple quoted strings can be used to include newlines and quotes without
   further escaping. If a triple quoted string is used across multiple lines,
   then the opening and closing triple quotes must appear at the end of the
   line. For example ::

     # flux: --setattr=user.conf="""
     # flux: [config]
     # flux:   item = "foo"
     # flux: """

Submission directives may be used to set default command line options for
:program:`flux batch` for a given script. Options given on the command line
override those in the submission script, e.g.: ::

   $ flux batch --job-name=test-name --wrap <<-EOF
   > #flux: -N4
   > #flux: --job-name=name
   > flux run -N4 hostname
   > EOF
   ƒ112345
   $ flux jobs -no {name} ƒ112345
   test-name



RESOURCES
=========

Flux: http://flux-framework.org

SEE ALSO
========

:man1:`flux-submit`, :man1:`flux-run`, :man1:`flux-alloc`,
:man1:`flux-bulksubmit`
