=====================
flux-config-ingest(5)
=====================


DESCRIPTION
===========

The Flux **job-ingest** service optionally modifies and validates job requests
before announcing new jobs to the **job-manager**. Configuration of the
**job-ingest** module can be accomplished via the ``ingest`` TOML table.
See the KEYS section below for supported ``ingest`` table keys.

The **job-ingest** module implements a two stage pipeline for job requests.
The first stage modifies jobspec and is implemented as a work crew of
``flux job-frobnicator`` processes.  The second stage validates the modified
requests and is implemented as a work crew of ``flux job-validator`` processes.
The frobnicator is disabled by default, and the validator is enabled by default.

The frobnicator and validator each supports a set of plugins, and each plugin
may consume additional arguments from the command line for specific
configuration.  The plugins and any arguments are configured in the
``ingest.frobnicator`` and ``ingest.validator`` TOML tables, respectively.
See the FROBNICATOR KEYS and VALIDATOR KEYS sections for supported keys.

KEYS
====

batch-count
   (optional) The job-ingest module batches sets of jobs together
   for efficiency. Normally this is done using a timer, but if the
   ``batch-count`` key is nonzero then jobs are batched based on a counter
   instead. This is mostly useful for testing.

buffer-size
   (optional) Set the input buffer size for job-ingest module workers.
   The value is string indicating the buffer size with optional SI units
   (e.g. "102400", "4.5M", "1024K") The default value is ``10M``.

FROBNICATOR KEYS
================

disable
   (optional) A boolean indicating whether to disable job frobnication,
   usually for testing purposes.

plugins
   (optional) An array of frobnicator plugins to use.  The default value is
   ``[ "defaults", "constraints" ]`` which are needed for assigning configured
   jobspec defaults, and adding queue constraints, respectively.
   For a list of supported plugins on your system run
   ``flux job-frobnicator --list-plugins``

args
   (optional) An array of extra arguments to pass on the frobnicator
   command line. Valid arguments can be found by running
   ``flux job-frobnicator --plugins=LIST --help``

FROBNICATOR PLUGIN CONFIGURATION
--------------------------------

Job frobnicator plugins optionally support plugin-specific configuration
via keys based on the plugin name in the ``[ingest.frobnicator]``
table. Frobnicator plugin keys match the plugin name, and define a table
of keys specific to each plugin.

No frobnicator plugins distributed with Flux support configuration at
this time.


VALIDATOR KEYS
==============

disable
   (optional) A boolean indicating whether to disable job validation.
   Disabling the job validator is not recommended, but may be useful
   for testing or high job throughput scenarios.

plugins
   (optional) An array of validator plugins to use. The default
   value is ``[ "jobspec" ]``, which uses the Python Jobspec class as
   a validator.  For a list of supported plugins on your system run
   ``flux job-validator --list-plugins``

args
   (optional) An array of extra arguments to pass on the validator
   command line. Valid arguments can be found by running
   ``flux job-validator --plugins=LIST --help``

VALIDATOR PLUGIN CONFIGURATION
------------------------------

Job validator plugins optionally support plugin-specific configuration via
keys based on the plugin name in the ``[ingest.validator]`` table. Validator
plugin keys match the plugin name, and define a table of keys specific to
each plugin.

Plugins distributed with Flux and support configuration are described below.

require-instance
----------------

The ``[ingest.validator.require-instance]`` table supports the following
keys:

minnodes
   (optional) The minimum number of nodes at which to reject jobs that
   are not instances of Flux. Default is 0. This is an alternative to passing
   the ``--require-instance-minnodes=`` option on the validator command line.

mincores
   (optional) The minimum number of cores at which to reject jobs that
   are not instances of Flux. Default is 0. This is an alternative to passing
   the ``--require-instance-mincores=`` option on the validator command line.

EXAMPLE
=======

::

   [ingest.frobnicator]
   plugins = [ "defaults" ]

   [ingest.validator]
   plugins = [ "jobspec", "feasibility" ]
   args =  [ "--require-version=1" ]


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man5:`flux-config`
