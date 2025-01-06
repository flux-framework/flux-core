The `ENVIRONMENT`_ options allow control of the environment exported to jobs
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
     * ``$var`` will substitute :envvar:`var` from the current environment,
       falling back to the process environment. An error will be thrown
       if environment variable :envvar:`var` is not set.
     * ``${var}`` is equivalent to ``$var``
     * Advanced parameter substitution is not allowed, e.g. ``${var:-foo}``
       will raise an error.

   ``VAL`` may also contain a mustache template, in which case the template
   will be substituted in the job shell with the corresponding value before
   launching job tasks. See `MUSTACHE TEMPLATES`_ for more information.

   Examples:
       ``PATH=/bin``, ``PATH=$PATH:/bin``, ``FOO=${BAR}something``,
       ``PATH=${PATH}:/{{tmpdir}}/bin``

 * Otherwise, the rule is considered a pattern from which to match
   variables from the process environment if they do not exist in
   the generated environment. E.g. ``PATH`` will export :envvar:`PATH` from the
   current environment (if it has not already been set in the generated
   environment), and ``OMP*`` would copy all environment variables that
   start with :envvar:`OMP` and are not already set in the generated
   environment.  It is important to note that if the pattern does not match
   any variables, then the rule is a no-op, i.e. an error is *not* generated.

   Examples:
       ``PATH``, ``FLUX_*_PATH``, ``/^OMP.*/``

Since we always starts with a copy of the current environment,
the default implicit rule is ``*`` (or :option:`--env=*`). To start with an
empty environment instead, the ``-*`` rule or :option:`--env-remove=*` option
should be used. For example, the following will only export the current
:envvar:`PATH` to a job:

::

    flux run --env-remove=* --env=PATH ...


Since variables can be expanded from the currently built environment, and
:option:`--env` options are applied in the order they are used, variables can
be composed on the command line by multiple invocations of :option:`--env`,
e.g.:

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
starting with :envvar:`OMP` from the current environment, set ``FOO=bar``,
and then set ``BAR=bar/baz``.

