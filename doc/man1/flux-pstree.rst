.. flux-help-include: true
.. flux-help-section: jobs

==============
flux-pstree(1)
==============


SYNOPSIS
========

**flux** **pstree** [*OPTIONS*] [JOBID ...]

DESCRIPTION
===========

.. program:: flux pstree

:program:`flux pstree` displays a tree of running jobs by job name, similar to
what the :linux:man1:`pstree` command does for system processes.

Like :command:`pstree`, identical leaves of the job tree are combined, which
results in a more compact output when many jobs within a Flux instance
share the same job name.

The :program:`flux pstree` command supports custom labels for jobs, including
separately labeling parent jobs, using the same format string syntax
supported by :man1:`flux-jobs`.

The command lists actively running jobs by default, but a :option:`--all`
option lists all jobs in all states for the current user. In the case
that :option:`-all` is used, the job labels will automatically be amended to
include the job status (i.e. ``{name}:{status_abbrev}``), though this
can be overridden on the command line.

The :program:`flux pstree` command additionally supports listing extended
job information before the tree display with the :option:`--extended`,
:option:`--details=NAME`, or :option:`--detail-format=FORMAT`  options, e.g.

::

  $ flux pstree -x
       JOBID USER     ST NTASKS NNODES  RUNTIME
  ƒJqUHUCzX9 user1     R      2      2   10.68s flux
     ƒe1j54L user1     R      1      1   8.539s ├── flux
     ƒuusNLo user1     R      1      1   5.729s │   ├── sleep
     ƒutPP4T user1     R      1      1   5.731s │   └── sleep
     ƒe1j54K user1     R      1      1   8.539s └── flux
    ƒ2MYrwzf user1     R      1      1   4.736s     └── sleep

Several detail formats are available via the :option:`-d, --details=NAME`
option, including progress, resources, and stats. For example, the
``progress`` display attempts to show the overall progress and
utilization of all Flux instances in a hierarchy by displaying the
total number of jobs in that instance (``NJOBS``), the "progress"
(``PROG`` - inactive/finished jobs divided by total jobs), and
core and GPU utilization (``CORE%`` and ``GPU%`` - number of used
resources divided by total available resources):

::

  $ flux pstree --details=progress

       JOBID NTASKS  NJOBS  PROG CORE%  GPU%    RUNTIME
  ƒJqUHUCzX9      2      3    0% 37.5%          0:02:15 flux
     ƒe1j54L      1   1000   23%  100%          0:02:13 ├── flux
    ƒ2HmSnHd      1                             0:00:01 │   ├── sleep
    ƒ2Hjxo1K      1                             0:00:01 │   └── sleep
     ƒe1j54K      1   1000 11.5%  100%          0:02:13 └── flux
    ƒ2b6cPMS      1                             0:00:01     └── sleep


By default, :program:`flux pstree` truncates lines that exceed the current
value of the ``COLUMNS`` environment variable or the terminal width
if ``COLUMNS`` is not set. To disable truncation, use the :option:`--long`
option.


By default, the enclosing Flux instance, or root of the tree, is included
in output, unless extended details are displayed as when any of the
:option:`--extended`, :option:`--details=NAME` or
:option:`--detail-format=FORMAT` options are used, or if one or more jobids
are directly targeted with a ``JOBID`` argument. This behavior can be changed
via the :option:`--skip-root=[yes|no]` option.


OPTIONS
=======

.. option:: -a, --all

   Include jobs in all states, including inactive jobs.
   This is shorthand for :option:`--filter=pending,running,inactive`.

.. option:: -c, --count=N

   Limit output to N jobs at every level (default 1000).

.. option:: -f, --filter=STATE|RESULT

   Include jobs with specific job state or result. Multiple states or
   results can be listed separated by comma. See the :ref:`flux_jobs_job_status`
   section of the :man1:`flux-jobs` manual for more detail.

.. option:: -l, --long

   Do not truncate long lines at ``COLUMNS`` characters.

.. option:: -p, --parent-ids

   Prepend jobid to parent labels.

.. option:: -L, --level=N

   Only descend *N* levels of the job hierarchy.

.. option:: -x, --extended

   Print extended details before tree output. This is the same as
   :option:`--details=default`.

.. option:: -d, --detail=NAME

   Select a named extended details format. The list of supported names
   can be seen in :option:`flux pstree --help` output.

.. option:: -n, --no-header

   For output with extended details, do not print header row.

.. option:: -X, --no-combine

   Typically, identical child jobs that are leaves in the tree display
   are combined as ``n*[label]``. With this option, the combination of
   like jobs is disabled.

.. option:: -o, --label=FORMAT

   Specify output format for node labels using Python format strings.
   Supports all format fields supported by :man1:`flux-jobs`.

.. option:: --parent-label=FORMAT

   Label tree parents with a different format than child jobs.

.. option:: --detail-format=FORMAT

   Specify an explicit details format to display before the tree part.
   Care should be taken that each line of the format is the same width
   to ensure that the tree display is rendered correctly (i.e. by judicious
   use of format field widths, e.g. ``{id.f58:>12}`` instead of just
   ``{id.f58}``.

.. option:: --skip-root=yes|no

   Explicitly skip (yes)  or force (no) display of the enclosing instance,
   or root of the tree, in output.

.. option:: -C, --compact

   Use compact tree connectors. Usefully for deep hierarchies.

.. option:: --ascii

   Use ascii tree connectors.


EXAMPLES
========

The default output of :program:`flux pstree` shows all running jobs for the
current user by name, including any running sub-jobs. If there are
currently no running jobs for the current user, only the enclosing
instance is displayed as a ``.``, to indicate the root of the tree:

::

  $ flux pstree
  .


If there is a running job, it is displayed under the root instance,
and includes all child jobs. Identical children are combined:

::

  $ flux pstree
  .
  └── flux
      ├── flux
      │   └── 2*[sleep]
      └── flux
          └── sleep
  

Extra information can be added to parents, which are instances of
flux. For example, summary job stats can be easily added:

::

  $ flux pstree --skip-root=yes --parent-label='{name} {instance.stats}'
  flux PD:1 R:2 CD:0 F:0
  ├── flux PD:592 R:2 CD:406 F:0
  │   └── 2*[sleep]
  └── flux PD:794 R:1 CD:205 F:0
      └── sleep
  
Or utilization:

::

  $ flux pstree --skip-root=yes \
    --parent-label='cores={instance.resources.all.ncores} {instance.utilization!P}' \
  cores=8 37.5%
  ├── cores=2 100%
  │   └── 2*[sleep]
  └── cores=1 100%

Displaying jobs in all states automatically adds the job *status* to the
display, which offers a compact representation of the state of jobs
throughout a hierarchy:

::

  $ flux pstree -a
  .
  ├── flux
  │   ├── flux:PD
  │   ├── flux
  │   │   ├── 824*[sleep:PD]
  │   │   ├── 2*[sleep:R]
  │   │   └── 174*[sleep:CD]
  │   └── flux
  │       ├── 914*[sleep:PD]
  │       ├── sleep:R
  │       └── 85*[sleep:CD]
  ├── flux:CA
  ├── 36*[flux:CD]
  ├── hostname:CA
  └── hostname:CD
  


RESOURCES
=========

Flux: http://flux-framework.org

SEE ALSO
========

:man1:`flux-jobs`
