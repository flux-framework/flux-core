.. note::
   Paths specified in the options :option:`--output`, :option:`--error`,
   and :option:`--input` will be opened relative to the working directory
   of the job on the nodes on which the job is running, not on the node
   from which the job is submitted.

.. option:: --input=FILENAME|RANKS

   Redirect stdin to the specified filename, bypassing the KVS.
   As a special case for ``flux run``, the argument may specify
   an idset of task ranks in to which to direct standard input.

.. option:: --output=TEMPLATE

   Specify the filename *TEMPLATE* for stdout redirection, bypassing
   the KVS.  *TEMPLATE* may be a mustache template. If the mustache template
   contains node or task-specific tags, then a different output file will
   be opened per node or task, respectively.
   See `MUSTACHE TEMPLATES`_ below for more information.

.. option:: --error=TEMPLATE

   Redirect stderr to the specified filename *TEMPLATE*, bypassing the KVS.
   *TEMPLATE* is expanded as described above.

.. option:: -u, --unbuffered

   Disable buffering of standard input and output as much as practical.
   Normally, stdout from job tasks is line buffered, as is stdin when
   running a job in the foreground via :man1:`flux-run`. Additionally,
   job output may experience a delay due to batching of output
   events by the job shell. With the :option:`--unbuffered` option,
   ``output.*.buffer.type=none`` is set in jobspec to request no buffering
   of output, and the default output batch period is reduced greatly,
   to make output appear in the KVS and printed to the standard output
   of :man1:`flux-run` as soon as possible. The :option:`--unbuffered` option
   is also passed to ``flux job attach``, which makes stdin likewise
   unbuffered. Note that the application and/or terminal may have
   additional input and output buffering which this option will not
   affect. For instance, by default an interactive terminal in canonical
   input mode will process input by line only. Likewise, stdout of a
   process run without a terminal may be fully buffered when using
   libc standard I/O streams (See NOTES in :linux:man3:`stdout`).

.. option:: -l, --label-io

   Add task rank prefixes to each line of output.

