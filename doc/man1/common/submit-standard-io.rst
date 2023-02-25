STANDARD I/O
============

By default, task stdout and stderr streams are redirected to the
KVS, where they may be accessed with the ``flux job attach`` command.

In addition, :man1:`flux-run` processes standard I/O in real time,
emitting the job's I/O to its stdout and stderr.

**--input=FILENAME**
   Redirect stdin to the specified filename, bypassing the KVS.

**--output=TEMPLATE**
   Specify the filename *TEMPLATE* for stdout redirection, bypassing
   the KVS.  *TEMPLATE* may be a mustache template which supports the
   tags *{{id}}* and *{{jobid}}* which expand to the current jobid
   in the F58 encoding.  If needed, an alternate encoding can be
   selected by using a subkey with the name of the desired encoding,
   e.g. *{{id.dec}}*. Supported encodings include *f58* (the default),
   *dec*, *hex*, *dothex*, and *words*. For :man1:`flux-batch` the
   default *TEMPLATE* is *flux-{{id}}.out*. To force output to KVS so it is
   available with ``flux job attach``, set *TEMPLATE* to *none* or *kvs*.

**--error=TEMPLATE**
   Redirect stderr to the specified filename *TEMPLATE*, bypassing the KVS.
   *TEMPLATE* is expanded as described above.

**-l, --label-io**
   Add task rank prefixes to each line of output.

