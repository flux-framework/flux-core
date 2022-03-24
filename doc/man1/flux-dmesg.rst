.. flux-help-description: manipulate broker log ring buffer

=============
flux-dmesg(1)
=============


SYNOPSIS
========

**flux** **dmesg** [*OPTIONS*]


DESCRIPTION
===========

Each broker rank maintains a circular buffer of log entries
which can be printed using flux-dmesg(1).


OPTIONS
=======

**-C, --clear**
   Clear the ring buffer.

**-c, --read-clear**
   Clear the ring buffer after printing its contents.

**-f, --follow**
   After printing the contents of the ring buffer, wait for new entries
   and print them as they arrive.

**-n, --new**
   Follow only new log entries.


EXAMPLES
========

To dump the ring buffer on all ranks

::

   $ flux exec flux dmesg | sort


RESOURCES
=========

Flux: http://flux-framework.org


SEE ALSO
========

:man1:`flux-setattr`, :man7:`flux-broker-attributes`
