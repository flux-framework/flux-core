.. option:: --conf=FILE|KEY=VAL|STRING|NAME

   The :option:`--conf` option allows configuration for a Flux instance
   started via ``flux-batch(1)`` or ``flux-alloc(1)`` to be iteratively built
   on the command line. On first use, a ``conf.json`` entry is added to the
   internal jobspec file archive, and ``-c{{tmpdir}}/conf.json`` is added
   to the flux broker command line. Each subsequent use of the :option:`--conf`
   option updates this configuration.

   The argument to :option:`--conf` may be in one of several forms:

   * A multiline string, e.g. from a batch directive. In this case the string
     is parsed as JSON or TOML::

      # flux: --conf="""
      # flux: [resource]
      # flux: exclude = "0"
      # flux: """

   * A string containing a ``=`` character, in which case the argument is
     parsed as ``KEY=VAL``, where ``VAL`` is parsed as JSON, e.g.::

      --conf=resource.exclude=\"0\"

   * A string ending in ``.json`` or ``.toml``, in which case configuration
     is loaded from a JSON or TOML file.

   * If none of the above conditions match, then the argument ``NAME`` is
     assumed to refer to a "named" config file ``NAME.toml`` or ``NAME.json``
     within the following search path, in order of precedence:

     - ``XDG_CONFIG_HOME/flux/config`` or ``$HOME/.config/flux/config`` if
       :envvar:`XDG_CONFIG_HOME` is not set

     - ``$XDG_CONFIG_DIRS/flux/config`` or ``/etc/xdg/flux/config`` if
       :envvar:`XDG_CONFIG_DIRS` is not set. Note that :envvar:`XDG_CONFIG_DIRS`
       may be a colon-separated path.

.. option:: --bg

   *(alloc only)* Do not interactively attach to the instance. Instead,
   print jobid on stdout once the instance is ready to accept jobs. The
   instance will run indefinitely until a time limit is reached, the
   job is canceled, or it is shutdown with ``flux shutdown JOBID``
   (preferred). If a COMMAND is given then the job will run until COMMAND
   completes. Note that ``flux job attach JOBID`` cannot be used to
   interactively attach to the job (though it will print any errors or
   output).

.. option:: -B, --broker-opts=OPT

   Pass specified options to the Flux brokers of the new instance. This
   option may be specified multiple times.

.. option:: --wrap

   *(batch only)* The :option:`--wrap` option wraps the specified COMMAND and
   ARGS in a shell script, by prefixing with ``#!/bin/sh``. If no COMMAND is
   present, then a SCRIPT is read on stdin and wrapped in a /bin/sh script.

.. option:: --dump=[FILE]

   When the job script is complete, archive the Flux
   instance's KVS content to ``FILE``, which should have a suffix known
   to :linux:man3:`libarchive`, and may be a mustache template as described
   above for :option:`--output`.  The content may be unarchived directly or
   examined within a test instance started with the
   :option:`flux-start --recovery` option.  If ``FILE`` is unspecified,
   ``flux-{{jobid}}-dump.tgz`` is used.

.. option:: --quiet

   Suppress logging of jobids to stdout
