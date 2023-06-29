STANDARD I/O
============

By default, task stdout and stderr streams are redirected to the
KVS, where they may be accessed with the ``flux job attach`` command.

In addition, :man1:`flux-run` processes standard I/O in real time,
emitting the job's I/O to its stdout and stderr.

**--input=FILENAME|RANKS**
   Redirect stdin to the specified filename, bypassing the KVS.
   As a special case for ``flux run``, the argument may specify
   an idset of task ranks in to which to direct standard input.

**--output=TEMPLATE**
   Specify the filename *TEMPLATE* for stdout redirection, bypassing
   the KVS.  *TEMPLATE* may be a mustache template which supports the
   following tags:

   *{{id}}* or *{{jobid}}*
     Expands to the current jobid in the F58 encoding. If needed, an
     alternate encoding may be selected by using a subkey with the name
     of the desired encoding, e.g. *{{id.dec}}*. Supported encodings
     include *f58* (the default), *dec*, *hex*, *dothex*, and *words*.

   *{{name}}*
     Expands to the current job name. If a name is not set for the job,
     then the basename of the command will be used.

   For :man1:`flux-batch` the default *TEMPLATE* is *flux-{{id}}.out*.
   To force output to KVS so it is available with ``flux job attach``,
   set *TEMPLATE* to *none* or *kvs*.

**--error=TEMPLATE**
   Redirect stderr to the specified filename *TEMPLATE*, bypassing the KVS.
   *TEMPLATE* is expanded as described above.

**-u, --unbuffered**
   Disable buffering of standard input and output as much as practical.
   Normally, stdout from job tasks is line buffered, as is stdin when
   running a job in the foreground via :man1:`flux-run`. Additionally,
   job output may experience a delay due to batching of output
   events by the job shell. With the ``--unbuffered`` option,
   ``output.*.buffer.type=none`` is set in jobspec to request no buffering
   of output, and the default output batch period is reduced greatly,
   to make output appear in the KVS and printed to the standard output
   of :man1:`flux-run` as soon as possible. The ``--unbuffered`` option
   is also passed to ``flux job attach``, which makes stdin likewise
   unbuffered. Note that the application and/or terminal may have
   additional input and output buffering which this option will not
   affect. For instance, by default an interactive terminal in canonical
   input mode will process input by line only. Likewise, stdout of a
   process run without a terminal may be fully buffered when using
   libc standard I/O streams (See NOTES in :linux:man3:`stdout`).

**-l, --label-io**
   Add task rank prefixes to each line of output.

