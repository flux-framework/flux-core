.. option:: -q, --queue=NAME

   Submit a job to a specific named queue. If a queue is not specified
   and queues are configured, then the jobspec will be modified at ingest
   to specify the default queue. If queues are not configured, then this
   option is ignored, though :man1:`flux-jobs` may display the queue
   name in its rendering of the ``{queue}`` attribute.

.. option:: -B, --bank=NAME

   Set the bank name for this job to ``NAME``. This option is equivalent
   to `--setattr=bank=NAME`, and results in the ``bank`` attribute being
   set to ``NAME`` in the submitted jobspec. However, besides the bank
   name appearing in job listing output, this option may have no effect
   if no plugin or package that supports it (such as flux-accounting)
   is installed and configured.

.. option:: -t, --time-limit=MINUTES|FSD

   Set a time limit for the job in either minutes or Flux standard duration
   (RFC 23). FSD is a floating point number with a single character units
   suffix ("s", "m", "h", or "d"). The default unit for the
   :option:`--time-limit` option is minutes when no units are otherwise
   specified. If the time limit is unspecified, the job is subject to the
   system default time limit.

.. option:: --job-name=NAME

   Set an alternate job name for the job.  If not specified, the job name
   will default to the command or script executed for the job.

.. option:: --flags=FLAGS

   Set comma separated list of job submission flags.  The possible flags are
   ``waitable``, ``novalidate``, and ``debug``.  The ``waitable`` flag will
   allow the job to be waited on via ``flux job wait`` and similar API calls.
   The ``novalidate`` flag will inform flux to skip validation of a job's
   specification.  This may be useful for high throughput ingest of a large
   number of jobs.  Both ``waitable`` and ``novalidate`` require instance
   owner privileges.  ``debug`` will output additional debugging into the job
   eventlog.

