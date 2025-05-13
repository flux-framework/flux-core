.. option:: --taskmap=SCHEME[:VALUE]

   Choose an alternate method for mapping job task IDs to nodes of the
   job. The job shell maps tasks using a "block" distribution scheme by
   default (consecutive tasks share nodes) This option allows the
   activation of alternate schemes by name, including an optional *VALUE*.
   Supported schemes which are built in to the job shell include

   cyclic[:N]
    Tasks are distributed over consecutive nodes with a stride of *N*
    (where N=1 by default).

   manual:TASKMAP
    An explicit RFC 34 taskmap is provided and used to manually map
    task ids to nodes. The provided *TASKMAP* must match the total number
    of nodes and tasks in the job, so this option is not useful unless
    the total number of nodes are known at job submission time.

   hostfile:FILE
    Assign tasks in order to hosts as they appear in FILE. FILE should
    have one or more lines each of which contains a host name or RFC
    29 Hostlist string. Each host assigned to the job must appear in
    the hostfile.  If there are less hosts in the hostfile than tasks
    in the job, then the list of hosts will be reused.

   However, shell plugins may provide other task mapping schemes, so
   check the current job shell configuration for a full list of supported
   taskmap schemes.

.. option:: --cc=IDSET

   *(submit,bulksubmit)* Replicate the job for each ``id`` in ``IDSET``.
   :envvar:`FLUX_JOB_CC` will be set to ``id`` in the environment of each
   submitted job to allow the job to alter its execution based on the
   submission index.  (e.g. for reading from a different input file). When
   using :option:`--cc`, the substitution string ``{cc}`` may be used in
   options and commands and will be replaced by the current ``id``.

.. option:: --bcc=IDSET

   *(submit,bulksubmit)* Identical to :option:`--cc`, but do not set
   :envvar:`FLUX_JOB_CC` in each job. All jobs will be identical copies.
   As with :option:`--cc`, ``{cc}`` in option arguments and commands will be
   replaced with the current ``id``.

.. option:: --quiet

   Suppress logging of jobids to stdout

.. option:: --log=FILE

   *(submit,bulksubmit)* Log command output and stderr to ``FILE``
   instead of the terminal. If a replacement (e.g. ``{}`` or ``{cc}``)
   appears in ``FILE``, then one or more output files may be opened.
   For example, to save all submitted jobids into separate files, use::

      flux submit --cc=1-4 --log=job{cc}.id hostname

.. option:: --log-stderr=FILE

   *(submit,bulksubmit)* Separate stderr into ``FILE`` instead of sending
   it to the terminal or a ``FILE`` specified by :option:`--log`.

.. option:: --wait

   *(submit,bulksubmit)* Wait on completion of all jobs before exiting.
   This is equivalent to :option:`--wait-event=clean`.

.. option:: --wait-event=NAME

   Wait until job or jobs have received event ``NAME``
   before exiting. E.g. to submit a job and block until the job begins
   running, use :option:`--wait-event=start`. *(submit,bulksubmit only)* If
   ``NAME`` begins with ``exec.``, then wait for an event in the exec eventlog,
   e.g.  ``exec.shell.init``. For ``flux run`` the argument to this option
   when used is passed directly to ``flux job attach``.

.. option:: --watch

   *(submit,bulksubmit)* Display output from all jobs. Implies :option:`--wait`.

.. option:: --progress

   *(submit,bulksubmit)* With :option:`--wait`, display a progress bar showing
   the progress of job completion. Without :option:`--wait`, the progress bar
   will show progress of job submission.

.. option:: --jps

   *(submit,bulksubmit)* With :option:`--progress`, display throughput
   statistics (jobs/s) in the progress bar.

.. option:: --define=NAME=CODE

   *(bulksubmit)* Define a named method that will be made available as an
   attribute during command and option replacement. The string being
   processed is available as ``x``. For example::

   $ seq 1 8 | flux bulksubmit --define=pow="2**int(x)" -n {.pow} ...

.. option:: --shuffle

   *(bulksubmit)* Shuffle the list of commands before submission.

.. option:: --sep=STRING

   *(bulksubmit)* Change the separator for file input. The default is
   to separate files (including stdin) by newline. To separate by
   consecutive whitespace, specify :option:`--sep=none`.
