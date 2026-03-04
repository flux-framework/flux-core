=============
flux-dmesg(1)
=============


SYNOPSIS
========

**flux** **dmesg** [*OPTIONS*]


DESCRIPTION
===========

.. program:: flux dmesg

Each broker rank maintains a circular buffer of log entries
which can be printed using :program:`flux dmesg`.


OPTIONS
=======

.. option:: -C, --clear

   Clear the ring buffer.

.. option:: -c, --read-clear

   Clear the ring buffer after printing its contents.

.. option:: -f, --follow

   After printing the contents of the ring buffer, wait for new entries
   and print them as they arrive.

.. option:: -n, --new

   Follow only new log entries.

.. option:: -H, --human

   Display human-readable output. See also :option:`--color` and
   :option:`--delta`.

.. option:: -d, --delta

   With :option:`--human`, display the time delta between messages instead
   of a relative offset since the last absolute timestamp.

.. option:: -L, --color[=WHEN]

   .. include:: common/color.rst

EXAMPLES
========

To dump the ring buffer on all ranks

::

   $ flux exec flux dmesg | sort


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-setattr`, :man7:`flux-broker-attributes`
