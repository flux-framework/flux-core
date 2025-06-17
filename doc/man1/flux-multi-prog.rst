==================
flux-multi-prog(1)
==================


SYNOPSIS
========

**flux** **multi-prog** [OPTIONS] CONFIG

DESCRIPTION
===========

.. program:: flux multi-prog

:program:`flux multi-prog` allows a parallel job to run a different
executable and arguments for each task, also known as multiple program,
multiple data (MPMD). It is used with :man1:`flux-run` or :man1:`flux-submit`
in place of the parallel command and args like::

  flux run -N4 flux multi-prog myapp.conf

The configuration file format is described in the :ref:`CONFIGURATION`
section below.

OPTIONS
=======

.. option:: -n, --dry-run=RANKS

  Do not run anything, instead print what would be run on *RANKS* specified
  as an idset of task ranks. This option is useful for testing a config file
  and should not be used with :man1:`flux-run` or :man1:`flux-submit`.

CONFIGURATION
=============

The :program:`flux multi-prog` configuration file defines the executable
and arguments to be run for each task rank in a Flux job. Each non-empty
line specifies a set of task ranks and the corresponding command to execute.

LINE FORMAT
^^^^^^^^^^^
Each line must begin with an RFC 22-compliant task idset, indicating the
ranks to which the command applies. Alternatively, the special wildcard ``*``
may be used to match any task rank not explicitly handled by other lines.

The task idset is followed by the executable and its arguments. For example:

::

  0-1 myapp arg1 arg2
  2-3 myapp arg3 arg4

In the above example:

 - Tasks 0 and 1 will execute :command:`myapp arg1 arg2`
 - Tasks 2 and 3 will execute :command:`myapp arg3 arg4`

.. note::

    Each task rank must match at most one line.

    If no matching line is found for a task, and * is not present,
    the task will have no command assigned and will fail to launch.


Lines are parsed using Python's ``shlex`` module, which supports shell-like
quoting and comments. For example:
::

  # this line is a comment
  0-1 myapp "quoted arg" arg2 # Inline comment

SUBSTITUTIONS
^^^^^^^^^^^^^
Two special tokens may be used within the command and argument strings:

**%t**
 Replaced with the task's global rank (task ID)

**%o**
  Replaced with the task's offset within the specified idset

For example:

::

  0-1 echo task %t
  2-3 echo task %t offset %o

Would produce the following output:

::

 0: task 0
 1: task 1
 2: task 2 offset 0
 3: task 3 offset 1

RESOURCES
=========

.. include:: common/resources.rst

SEE ALSO
========

:man1:`flux-run`, :man1:`flux-submit`
