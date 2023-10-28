ENVIRONMENT
===========

By default, these commands duplicate the current environment when submitting
jobs. However, a set of environment manipulation options are provided to
give fine control over the requested environment submitted with the job.

.. option:: --env=RULE

   Control how environment variables are exported with *RULE*. See
   *ENV RULE SYNTAX* section below for more information. Rules are
   applied in the order in which they are used on the command line.
   This option may be specified multiple times.

.. option:: --env-remove=PATTERN

   Remove all environment variables matching *PATTERN* from the current
   generated environment. If *PATTERN* starts with a ``/`` character,
   then it is considered a :linux:man7:`regex`, otherwise *PATTERN* is
   treated as a shell :linux:man7:`glob`. This option is equivalent to
   ``--env=-PATTERN`` and may be used multiple times.

.. option:: --env-file=FILE

   Read a set of environment *RULES* from a *FILE*. This option is
   equivalent to ``--env=^FILE`` and may be used multiple times.

ENV RULES
=========

The ``--env*`` options allow control of the environment exported to jobs
via a set of *RULE* expressions. The currently supported rules are

 * If a rule begins with ``-``, then the rest of the rule is a pattern
   which removes matching environment variables. If the pattern starts
   with ``/``, it is a :linux:man7:`regex`, optionally ending with
   ``/``, otherwise the pattern is considered a shell
   :linux:man7:`glob` expression.

   Examples:
      ``-*`` or ``-/.*/`` filter all environment variables creating an
      empty environment.

 * If a rule begins with ``^`` then the rest of the rule is a filename
   from which to read more rules, one per line. The ``~`` character is
   expanded to the user's home directory.

   Examples:
      ``~/envfile`` reads rules from file ``$HOME/envfile``

 * If a rule is of the form ``VAR=VAL``, the variable ``VAR`` is set
   to ``VAL``. Before being set, however, ``VAL`` will undergo simple
   variable substitution using the Python ``string.Template`` class. This
   simple substitution supports the following syntax:

     * ``$$`` is an escape; it is replaced with ``$``
     * ``$var`` will substitute ``var`` from the current environment,
       falling back to the process environment. An error will be thrown
       if environment variable ``var`` is not set.
     * ``${var}`` is equivalent to ``$var``
     * Advanced parameter substitution is not allowed, e.g. ``${var:-foo}``
       will raise an error.

   Examples:
       ``PATH=/bin``, ``PATH=$PATH:/bin``, ``FOO=${BAR}something``

 * Otherwise, the rule is considered a pattern from which to match
   variables from the process environment if they do not exist in
   the generated environment. E.g. ``PATH`` will export ``PATH`` from the
   current environment (if it has not already been set in the generated
   environment), and ``OMP*`` would copy all environment variables that
   start with ``OMP`` and are not already set in the generated environment.
   It is important to note that if the pattern does not match any variables,
   then the rule is a no-op, i.e. an error is *not* generated.

   Examples:
       ``PATH``, ``FLUX_*_PATH``, ``/^OMP.*/``

Since we always starts with a copy of the current environment,
the default implicit rule is ``*`` (or ``--env=*``). To start with an
empty environment instead, the ``-*`` rule or ``--env-remove=*`` option
should be used. For example, the following will only export the current
``PATH`` to a job:

::

    flux run --env-remove=* --env=PATH ...


Since variables can be expanded from the currently built environment, and
``--env`` options are applied in the order they are used, variables can
be composed on the command line by multiple invocations of ``--env``, e.g.:

::

    flux run --env-remove=* \
                  --env=PATH=/bin --env='PATH=$PATH:/usr/bin' ...

Note that care must be taken to quote arguments so that ``$PATH`` is not
expanded by the shell.


This works particularly well when specifying rules in a file:

::

    -*
    OMP*
    FOO=bar
    BAR=${FOO}/baz

The above file would first clear the environment, then copy all variables
starting with ``OMP`` from the current environment, set ``FOO=bar``,
and then set ``BAR=bar/baz``.

