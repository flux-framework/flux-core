==============
flux-config(5)
==============

DESCRIPTION
===========

Flux normally operates without configuration files.  If configuration is
needed, the :man1:`flux-broker` **--config-path=PATH** option may be used
to instruct Flux to parse a config file or, if **PATH** is a directory, all
files matching the glob **PATH/*.toml**.  Alternatively, the
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
multiple files.  For example, the Flux Administrator's Guide suggests one
file per top level TOML table.

If the **PATH** value is a regular file, the configuration may optionally be
provided as JSON. This allows the output of ``flux config get`` to be read
by another Flux instance. A JSON config file must have the file extension
``.json``, otherwise it is assumed to be TOML.

Each Flux broker parses configuration files independently, however it is
assumed that config files are identical across the brokers of a given instance.
System administrators managing Flux configuration files should ensure that
this is the case, and be mindful that old ``.toml`` files must be removed from
the configuration directory or renamed so they no longer match the glob.

The configuration may be altered at runtime by changing the files, then running
``flux config reload`` on each broker, or ``systemctl reload flux`` on each
node of the system instance.  However see CAVEATS below.

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

Although configuration can be reloaded on a live system, it should be assumed
that configuration parameters take effect on the next Flux broker restart
unless otherwise noted in their documentation.  This means that some
parameters, such as the system instance network topology, must only be
changed in conjunction with a full system instance restart in order to avoid
brokers becoming out of sync if they are independently restarted before the
next instance restart.


RESOURCES
=========

.. include:: common/resources.rst

Flux Administrator's Guide: https://flux-framework.readthedocs.io/projects/flux-core/en/latest/guide/admin.html

TOML: Tom's Obvious Minimal Language: https://toml.io/en/


SEE ALSO
========

:man1:`flux-broker`, :man5:`flux-config-access`, :man5:`flux-config-bootstrap`,
:man5:`flux-config-tbon`, :man5:`flux-config-exec`, :man5:`flux-config-ingest`,
:man5:`flux-config-resource`, :man5:`flux-config-archive`,
:man5:`flux-config-job-manager`, :man5:`flux-config-kvs`
