
Submission commands support a simplified version of mustache templates
in select options (e.g., :option:`--output`, :option:`--error`,
:option:`--dump`), and job commands/arguments. These templates are replaced
by job details once available. A mustache template is a tag in double
braces, like *{{tag}}*.

The following is a list of currently supported tags:

.. note::
   The template system in the job shell allows plugins to extend the set
   of supported mustache tags. Check with your installation of Flux to
   determine if extra tags are available locally.

Job mustache tags:

*{{id}}* or *{{jobid}}*
  Expands to the current jobid in F58 encoding. If needed, an alternate
  encoding may be selected by using a subkey with the name of the
  desired encoding, e.g. *{{id.dec}}*. Supported encodings include *f58*
  (the default), *dec*, *hex*, *dothex*, and *words*.

*{{name}}*
  Expands to the current job name. If a name is not set for the job,
  then the basename of the command will be used.

*{{nnodes}}*
  Expands to the number of nodes allocated to the job.

*{{size}}* or *{{ntasks}}*
  Expands to the job size, or total number of tasks in the job.

Node-specific tags are prefixed with *node.*, and support any of the
keys present in the object returned by :man3:`flux_shell_get_rank_info`,
these include:

*{{node.id}}*
  Expands to the current node index within the job (0 - nnodes-1).

*{{node.name}}*
  Expands to the hostname of the current node.

*{{node.broker_rank}}*
  Expands to the broker rank of the enclosing instance on this node.

*{{node.cores}}*
  Expands to the core id list assigned to the current node in idset
  form. For example, ``0-3`` or ``1``

*{{node.gpus}}*
  Expands to the GPU id list assigned to the job on this node
  in idset form, e.g. ``0-1`` or ``4``

*{{node.ncores}}*
  Expands to the number of cores allocated to the job on this node.

Task-specific tags are prefixed with *task.*, with the special case of
*{{taskid}}*, which is an alias for *{{task.id}}*.


*{{task.id}}*, *{{task.rank}}*, or *{{taskid}}*
  Expands to the global rank for the current task.

*{{task.index}}* or *{{task.localid}}*
  Expands to the local rank for the current task.

Other tags:

*{{tmpdir}}*
  Expands to the path to the job temporary directory on the local system.

If an unknown mustache tag is encountered, an error message may be displayed,
and the unknown template will appear in the result.
