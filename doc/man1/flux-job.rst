.. flux-help-include: true

===========
flux-job(1)
===========


SYNOPSIS
========

**flux** **job** **cancel** [*OPTIONS*] *ids...* [*--*] [*message...*]

**flux** **job** **cancelall** [*OPTIONS*] [*message...*]

**flux** **job** **kill** [*--signal=SIG*] *id* [*id...*]

**flux** **job** **killall** [*OPTIONS*]

**flux** **job** **raise** [*OPTIONS*] *ids...* [*--*] [*message...*]

**flux** **job** **raiseall** [*OPTIONS*] *type* [*message...*]

**flux** **job** **purge** [*OPTIONS*]

DESCRIPTION
===========

flux-job(1) performs various job related housekeeping functions.

CANCEL
======

One or more jobs by may be canceled with ``flux job cancel``.  An optional
message included with the cancel exception may be provided via the *-m,
--message=NOTE* option or after the list of jobids. The special argument
*"--"* forces the end of jobid processing and can be used to separate the
exception message from the jobids when necessary.

**-m, --message=NOTE**
   Set the optional exception note. It is an error to specify the message
   via this option and on the command line after the jobid list.

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

One or more running jobs may be signaled by jobid with ``flux job kill``.

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

An exception may raised on one or more jobids with ``flux job raise``.
An optional message included with the job exception may be provided via
the *-m, --message=NOTE* option or after the list of jobids. The special
argument *"--"* forces the end of jobid processing and can be used to
separate the exception message from the jobids when necessary.

**-m, --message=NOTE**
   Set the optional exception note. It is an error to specify the message
   via this option and on the command line after the jobid list.
**-s, --severity=N**
   Set exception severity.  The severity may range from 0=fatal to
   7=least severe (default: 0).

**-t, --type=TYPE**
   Set exception type (default: cancel).

Exceptions may be raised in bulk with ``flux job raiseall``, which requires a
type (positional argument) and accepts the following options:

**-s, --severity=N**
   Set exception severity.  The severity may range from 0=fatal to
   7=least severe (default: 7).

**-u, --user=USER**
   Set target user.  The instance owner may specify *all* for all users.

**-S, --states=STATES**
   Set target job states (default: ACTIVE)

**-f, --force**
   Confirm the command.

PURGE
=====

Inactive job data may be purged from the Flux instance with ``flux job purge``.
The following options may be used to add selection criteria:

**--age-limit=FSD**
   Purge inactive jobs older than the specified Flux Standard Duration.

**--num-limit=COUNT**
   Purge the oldest inactive jobs until there are at most COUNT left.

**-f, --force**
   Confirm the command.

Inactive jobs may also be purged automatically if the job manager is
configured as described in :man5:`flux-config-job-manager`.


RESOURCES
=========

Flux: http://flux-framework.org

