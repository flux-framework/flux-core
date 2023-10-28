PROCESS RESOURCE LIMITS
=======================

By default these commands propagate some common resource limits (as described
in :linux:man2:`getrlimit`) to the job by setting the ``rlimit`` job shell
option in jobspec.  The set of resource limits propagated can be controlled
via the :option:`--rlimit=RULE` option:

.. option:: --rlimit=RULE

    Control how process resource limits are propagated with *RULE*. Rules
    are applied in the order in which they are used on the command line.
    This option may be used multiple times.

The :option:`--rlimit` rules work similar to the :option:`--env` option rules:

 * If a rule begins with ``-``, then the rest of the rule is a name or
   :linux:man7:`glob` pattern which removes matching resource limits from
   the set to propagate.

   Example:
     ``-*`` disables propagation of all resource limits.

 * If a rule is of the form ``LIMIT=VALUE`` then *LIMIT* is explicitly
   set to *VALUE*. If *VALUE* is ``unlimited``, ``infinity`` or ``inf``,
   then the value is set to ``RLIM_INFINITY`` (no limit).

   Example:
     ``nofile=1024`` overrides the current ``RLIMIT_NOFILE`` limit to 1024.

 * Otherwise, *RULE* is considered a pattern from which to match resource
   limits and propagate the current limit to the job, e.g.

      ``--rlimit=memlock``

   will propagate ``RLIMIT_MEMLOCK`` (which is not in the list of limits
   that are propagated by default).

We start with a default list of resource limits to propagate,
then applies all rules specified via :option:`--rlimit` on the command line.
Therefore, to propagate only one limit, ``-*`` should first be used to
start with an empty set, e.g. :option:`--rlimit=-*,core` will only propagate
the ``core`` resource limit.

The set of resource limits propagated by default includes all those except
``memlock``, ``ofile``, ``msgqueue``, ``nice``, ``rtprio``, ``rttime``,
and ``sigpending``. To propagate all possible resource limits, use
:option:`--rlimit=*`.

