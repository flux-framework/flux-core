.. Once we advance to sphinx 5.3+, :option: will x-ref with arguments
.. e.g. :option:`cpu-affinity=OFF`.  For now, leave options off to get x-ref.

.. list-table::
   :header-rows: 1

   * - Name
     - Description

   * - :option:`cpu-affinity`
     - Set task affinity to cores (:option:`off|per-task|map:LIST|on`)

   * - :option:`gpu-affinity`
     - Set task affinity to GPUs (:option:`off|per-task|map:LIST|on`)

   * - :option:`verbose`
     - Increase shell log verbosity (1 or 2).

   * - :option:`nosetpgrp`
     - Don't run each task in its own process group.

   * - :option:`pmi`
     - Set PMI service(s) for launched programs (:option:`off|simple|LIST`)

   * - :option:`stage-in`
     - Copy files previously mapped with :man1:`flux-filemap` to
       :envvar:`FLUX_JOB_TMPDIR`.

   * - :option:`pty.interactive`
     - Enable a pty on rank 0 for :program:`flux job attach`.

   * - :option:`exit-timeout`
     - Start fatal job exception timer after first task exits
       (:option:`none|FSD`)

   * - :option:`exit-on-error`
     - Raise a fatal job exception immediately if first task exits with
       nonzero exit code.

   * - :option:`hwloc.xmlfile`
     - Write hwloc XML gathered by job to a file and set ``HWLOC_XMLFILE``
