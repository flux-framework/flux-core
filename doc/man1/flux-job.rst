.. flux-help-include: true

===========
flux-job(1)
===========


SYNOPSIS
========

**flux** **job** **cancel** *id* [*message...*]

**flux** **job** **cancelall** [*OPTIONS*] [*message...*]

**flux** **job** **kill** [*--signal=SIG*] *id*

**flux** **job** **killall** [*OPTIONS*]

**flux** **job** **raise** [*OPTIONS*] *id* [*message...*]

**flux** **job** **raiseall** [*OPTIONS*] *type* [*message...*]

**flux** **job** **exec** [*OPTIONS*] [*--ntasks=N*] *COMMAND...*


DESCRIPTION
===========

flux-job(1) performs various job related housekeeping functions.

CANCEL
======

A single job may be canceled with ``flux job cancel``.

Jobs may be canceled in bulk with ``flux job cancelall``.  Target jobs are
selected with:

**-u, --user=USER**
   Set target user.  The instance owner may specify *all* for all users.

**-S, --states=STATES**
   Set target job states (default: ACTIVE).

**-f, --force**
   Confirm the command

**-q, --quiet**
   Suppress output if no jobs match

SIGNAL
======

Running jobs may be signaled with ``flux job kill``.

**-s, --signal=SIG**
   Send signal SIG (default: SIGTERM).

Running jobs may be signaled in bulk with ``flux job killall``.  In addition
to the option above, target jobs are selected with:

**-u, --user=USER**
   Set target user.  The instance owner may specify *all* for all users.

**-f, --force**
   Confirm the command.

EXCEPTION
=========

An exception may raised on a single job with ``flux job raise``.

**-s, --severity=N**
   Set exception severity.  The severity may range from 0=fatal to
   7=least severe (default: 0).

**-t, --type=TYPE**
   Set exception type (default: cancel).

Exceptions may be raised in bulk with ``flux job raiseall``.  In addition to
the severity option above, target jobs are selected with:

**-u, --user=USER**
   Set target user.  The instance owner may specify *all* for all users.

**-S, --states=STATES**
   Set target job states (default: ACTIVE)

**-f, --force**
   Confirm the command.

FAST EXEC
=========

``flux job exec`` is like ``flux mini run``, but faster for short jobs.
The reduction in overhead comes with the following limitations:

* It is only available to the Flux instance owner.

* There are no options to set urgency, job attributes, dependencies,
  begin-time, or submission flags.

* There is no way to manipulate the environment on the command line.

* Standard input is always redirected from /dev/null.

* Standard output is always sent to the ``flux job exec`` standard output,
  via the KVS.

The options are as follows:

**-n, --ntasks=N**
   Set the number of tasks to launch (default 1).

**-c, --cores-per-task=N**
   Set the number of cores to assign to each task (default 1).

**-g, --gpus-per-task=N**
   Set the number of GPU devices to assign to each task (default none).

**-N, --nodes=N**
   Set the number of nodes to assign to the job. Tasks will be distributed
   evenly across the allocated nodes. It is an error to request more nodes
   than there are tasks. If unspecified, the number of nodes will be chosen
   by the scheduler.

**-t, --time-limit=FSD**
   Set a time limit for the job in Flux standard duration (RFC 23).
   FSD is a floating point number with a single character units suffix
   ("s", "m", "h", or "d"). If unspecified, the job is subject to the
   system default time limit.

**-l, --label-io**
   Add task rank prefixes to each line of output.

**-o, --setopt=KEY[=VAL]**
   Set shell option. Keys may include periods to denote hierarchy.
   VAL is optional and may be valid JSON (bare values, objects, or arrays),
   otherwise VAL is interpreted as a string. If VAL is not set, then the
   default value is 1. See SHELL OPTIONS below.


RESOURCES
=========

Github: http://github.com/flux-framework

SEE ALSO
========

flux-mini(1)
