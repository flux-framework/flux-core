==============
flux-config(5)
==============

DESCRIPTION
===========

Flux normally operates without configuration files.  If configuration is
needed, the :option:`flux-broker --config-path` option may be used
to instruct Flux to parse a config file or, if the path argument is a directory,
all files in it matching the glob ***.toml**.  Alternatively, the
:envvar:`FLUX_CONF_DIR` environment variable may be used to set the
configuration file or directory path. If both are set, the command line
argument takes precedence.

The Flux systemd unit file starts the system instance broker with
``--config-path=${sysconfdir}/flux/system/conf.d``.  Further discussion of the
system instance configuration as a whole may be found in the Flux
Administrator's Guide.

Flux configuration files typically follow the TOML file format,
with configuration subdivided by function into separate TOML tables.
The tables may all appear in a single ``.toml`` file or be fragmented in
multiple files, for example, one file per top level TOML table.  If fragments
are used, system administrators should ensure that old fragments are removed
from the configuration directory or renamed so they no longer match the glob.

If the path argument is a regular file, the configuration may optionally be
provided as JSON. This allows the output of ``flux config get`` to be read
by another Flux instance. A JSON config file must have the file extension
``.json``, otherwise it is assumed to be TOML.

Each Flux broker independently parses configuration files, if any, at startup.
During initialization the follower brokers begin tracking the leader's
configuration, ignoring changes to the on-disk configuration.
Please note that:

  1. The on-disk configuration files provide the initial configuration for
     built-in broker services, including the overlay network.  Thus on a Flux
     system instance, tables such as ``[bootstrap]`` and ``[tbon]`` must be kept
     up-to-date on-disk.

  2. The configuration from the leader broker (rank 0) provides the initial
     configuration for the broker modules dynamically loaded in rc1.

  3. When the runtime configuration is changed on the leader broker using
     the :man1:`flux-config` command, e.g. to re-read TOML files or directly update
     configuration parameters, the changes are propagated to all follower brokers,
     although see the note about runtime updates in CAVEATS below.

.. _flux_config_caveats:

CAVEATS
=======

Although most Flux framework projects such as **fluxion** and
**flux-accounting** are built upon the **flux-core** configuration mechanism
described above, the **flux-security** project use a similar, but independent
TOML configuration.  The security components unconditionally parse ``*.toml``
from compiled-in directories: ``${sysconfdir}/flux/security/conf.d`` for the
signing library, and ``${sysconfdir}/flux/imp/conf.d`` for the IMP.
``flux config reload`` does not affect the security components.  Refer to the
Flux Administrator's Guide for more information on configuring security.

Some portions of the configuration use formats other than TOML:

- The ``resource.path`` key optionally points to a file in RFC 20 (R version 1) JSON format.
- The ``bootstrap.curve_cert`` key optionally points to a CURVE certificate in a ZeroMQ certificate file format.
For clarity, these files should be located outside of the TOML configuration
directory.

After Flux is running, the configuration can be updated, but not all changes
affect the running system.  In documentation, it should be assumed that changes
are enacted on the next Flux broker restart unless otherwise noted.

RESOURCES
=========

.. include:: common/resources.rst

Flux Administrator's Guide: https://flux-framework.readthedocs.io/projects/flux-core/en/latest/guide/admin.html

TOML: Tom's Obvious Minimal Language: https://toml.io/en/


SEE ALSO
========

:man1:`flux-broker`, :man5:`flux-config-access`, :man5:`flux-config-bootstrap`,
:man5:`flux-config-tbon`, :man5:`flux-config-exec`, :man5:`flux-config-ingest`,
:man5:`flux-config-resource`,
:man5:`flux-config-job-manager`, :man5:`flux-config-kvs`
