==============
flux-broker(1)
==============


SYNOPSIS
========

**flux-broker** [*OPTIONS*] [*initial-program* [*args...*]]

DESCRIPTION
===========

.. program:: flux broker

:program:`flux broker` is a distributed message broker daemon that provides
communications services within a Flux instance. It may be
launched as a parallel program under Flux or other resource managers
that support PMI.

Resource manager services are implemented as dynamically loadable
modules.

Brokers within a Flux instance are interconnected using
ZeroMQ sockets, and each is assigned a rank from 0 to size - 1.
The rank 0 node is the root of a tree-based overlay network.
This network may be accessed by Flux commands and modules
using Flux API services.

The rank 0 node is called the *leader*, while other ranks are called *followers*.

After its overlay network has completed wire-up, :program:`flux broker`
starts the initial program on rank 0. If none is specified on
the broker command line, an interactive shell is launched.


OPTIONS
=======

.. option:: -h, --help

   Summarize available options.

.. option:: -v, --verbose

   Be annoyingly chatty.

.. option:: -S, --setattr=ATTR=VAL

   Set initial value for broker attribute.

.. option:: -c, --config-path=PATH

   Set the PATH to broker configuration. If PATH is a directory, then
   read all TOML files from that directory. If PATH is a file, then load
   configuration as JSON if the file extension is ``.json``, otherwise
   load the file as TOML.

.. option:: --conf=VALUE

   Update the broker configuration from VALUE after any configuration
   file specified with :option:`--config-path` (or :envvar:`FLUX_CONF_DIR`)
   is loaded. This option may be specified multiple times and values are
   applied in order. The format of VALUE is determined as follows:

   ``KEY=VAL``
     If VALUE contains ``=``, set the configuration key at dotted path
     KEY to VAL. VAL is parsed as JSON; if it is not valid JSON it is
     treated as a string. Note: file paths containing ``=`` are
     interpreted as KEY=VAL rather than as file names.

   inline JSON object
     If VALUE starts with ``{``, it is parsed as an inline JSON object
     and merged into the configuration.

   inline TOML string
     If VALUE contains a newline (and does not start with ``{``), it is
     parsed as an inline TOML string and merged into the configuration.
     Note: shell command substitution (``$()``) strips trailing newlines,
     so inline TOML strings should have an embedded newline, as TOML
     naturally does.

   path ending in ``.json``
     Parse the file as JSON and merge the result into the configuration.

   any other string
     Treated as a path to a TOML file.

   .. note::

      When a broker is started with :option:`--conf`, that configuration is
      in-memory only and is not written to files. If :option:`--config-path`
      or :envvar:`FLUX_CONF_DIR` is also set, running :command:`flux config
      reload` within that instance will overwrite any configuration applied
      via :option:`--conf`, since reload reads from files only. (If no
      config path is set, :command:`flux config reload` is a no-op.)

.. _broker_logging:

LOGGING
=======

Brokers offer a logging service that includes a circular log buffer that
may be accessed with :man1:`flux-dmesg`.  Log messages are generated and
added to the buffer by Flux components that call :man3:`flux_log`, or by
scripts that call :man1:`flux-logger`.

Internally, the RFC 5424 Syslog format is used, therefore each message is
tagged with the following attributes:

severity
  An indication of how important the log message is:

  .. list-table::
     :header-rows: 1

     * - 0
       - :const:`LOG_EMERG`

     * - 1
       - :const:`LOG_ALERT`

     * - 2
       - :const:`LOG_CRIT`

     * - 3
       - :const:`LOG_ERR`

     * - 4
       - :const:`LOG_WARNING`

     * - 5
       - :const:`LOG_NOTICE`

     * - 6
       - :const:`LOG_INFO`

     * - 7
       - :const:`LOG_DEBUG`

procid
  Usually the process id of the component that generated the log message

appname
  The name of the component that generated the log message

hostname
  The broker rank that accepted the log message.  Flux uses numerical ranks
  here instead of hostnames to uniquely identify the accepting broker when
  a Flux instance has multiple brokers per node, as is common in test.

The disposition of log messages is controlled by the ``log-`` prefixed broker
attributes described in :man7:`flux-broker-attributes`.  These attributes may
be set on the broker command line or altered at runtime with :man1:`flux-setattr`.
The systemd unit file that launches brokers for the Flux system instance brokers
alters some of them on the command line.

.. list-table::
   :header-rows: 1

   * - behavior
     - default
     - system instance

   * - local ring buffer
     - accept all levels
     - accept all levels

   * - local stderr
     - print LOG_CRIT (2) and below
     - print LOG_INFO (6) and below

   * - leader stderr (collected)
     - print LOG_ERR (3) and below
     - none

   * - leader log file (collected)
     - none
     - none

   * - local syslog
     - none
     - none

RESOURCES
=========

.. include:: common/resources.rst

RFC 5424 The Syslog Protocol: https://tools.ietf.org/html/rfc5424

SEE ALSO
========

:man7:`flux-broker-attributes`
