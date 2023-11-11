ENVIRONMENT
===========

By default, these commands duplicate the current environment when submitting
jobs. However, a set of environment manipulation options are provided to
give fine control over the requested environment submitted with the job.

.. option:: --env=RULE

   Control how environment variables are exported with *RULE*. See
   the `ENV RULES`_ section below for more information. Rules are
   applied in the order in which they are used on the command line.
   This option may be specified multiple times.

.. option:: --env-remove=PATTERN

   Remove all environment variables matching *PATTERN* from the current
   generated environment. If *PATTERN* starts with a ``/`` character,
   then it is considered a :linux:man7:`regex`, otherwise *PATTERN* is
   treated as a shell :linux:man7:`glob`. This option is equivalent to
   :option:`--env=-PATTERN` and may be used multiple times.

.. option:: --env-file=FILE

   Read a set of environment *RULES* from a *FILE*. This option is
   equivalent to :option:`--env=^FILE` and may be used multiple times.

