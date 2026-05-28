.. flux-help-description: Flux administration subcommands

==============
flux-admin(1)
==============


SYNOPSIS
========

| **flux** **admin** **system-scripts** [*-v, --verbose*] [*--color=WHEN*]
| **flux** **admin** **cleanup-push** [*COMMAND...*]


DESCRIPTION
===========

:program:`flux admin` provides subcommands for Flux instance administration.


COMMANDS
========

system-scripts
--------------

.. program:: flux admin system-scripts

:program:`flux admin system-scripts` displays the status and configuration
of system scripts (prolog, epilog, and housekeeping) for the current Flux
instance.

For each script type (prolog, epilog, housekeeping), the command shows:

- Configuration status (enabled, not configured, or configured but inactive)
- Execution mode (per-rank=true or per-rank=false for prolog/epilog)
- Release settings (for housekeeping)
- The actual command that will execute (if using a custom command)
- Scripts that will be executed (if using the default flux-imp command)

**Script Execution Logic:**

System scripts from directories are only executed when the configured command
is the default :command:`flux-imp run <type>` command. When a custom command is
configured, the directory scripts are bypassed and only the custom command
executes.

When using the default command, scripts are discovered and executed in this order:

1. **system:** ``<libexecdir>/flux/<type>.d/`` - Package-provided scripts (always executed)
2. **site:** Either:

   - Legacy single-file script at ``<sysconfdir>/flux/system/<type>`` if present
     (this skips the site scripts directory for backwards compatibility), OR
   - Scripts in ``<sysconfdir>/flux/system/<type>.d/`` site directory

In other words, if a legacy single-file script exists, it runs instead of the
site scripts directory. System scripts always run regardless.

.. option:: -v, --verbose

   Show scripts even when the system is not configured. In non-verbose mode,
   only configured systems are shown with full details.

.. option:: --color=WHEN

   Control when to use color in output. *WHEN* can be ``always``, ``never``,
   or ``auto`` (default). The ``NO_COLOR`` environment variable also disables
   color output.


**EXAMPLES:**

Check prolog/epilog/housekeeping status::

    $ flux admin system-scripts
    prolog: enabled (per-rank=false)
      command: /usr/bin/custom-prolog.sh

    epilog: not configured
    housekeeping: not configured

View scripts that would execute with default command::

    $ flux admin system-scripts -v
    prolog: enabled (per-rank=false)
      system: /usr/libexec/flux/prolog.d
        ✓ 01-setup.sh
        ✓ 99-finalize.sh
      site: /etc/flux/system/prolog.d
        ✓ 10-custom.sh

    epilog: not configured
    housekeeping: not configured

cleanup-push
------------

.. program:: flux admin cleanup-push

:program:`flux admin cleanup-push` adds a command to run after completion of
the initial program, before rc3. The command is pushed to the front of the
list of cleanup commands.

If *COMMAND* is not provided as arguments, commands are read one per line
from standard input and pushed in reverse order to retain their order.

**EXAMPLE:**

Add a cleanup command::

    $ flux admin cleanup-push "rm -rf /tmp/flux-job-*"


RESOURCES
=========

.. include:: common/resources.rst


SEE ALSO
========

:man1:`flux-config`, :man1:`flux-housekeeping`, :man7:`flux-jobtap-plugins`
