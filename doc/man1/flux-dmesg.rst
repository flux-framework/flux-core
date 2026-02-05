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

   Colorize output. The optional argument *WHEN* can be *auto*, *never*,
   or *always*. If *WHEN* is omitted, it defaults to *always*. The default
   value when the :option:`--color` option is not used is *auto*.

.. option:: --stderr-level=LEVEL

   Set the broker stderr log severity level.  Log messages with a severity
   less than (more severe) or equal to the threshold are printed on the
   local broker's standard error.  LEVEL should be a numerical syslog level
   (0-7) or a negative number to disable.  The initial value is 3 (LOG_ERR)
   unless overridden with the ``log-stderr-level`` broker attribute.
   For a table of level values, see :man1:`flux-broker`.

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
