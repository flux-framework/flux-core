*************
Configuration
*************

Much of Flux configuration occurs via
`TOML <https://github.com/toml-lang/toml>`_ configuration files found in a
hierarchy under ``/etc/flux``.  There are three separate TOML configuration
spaces:  one for flux-security, one for the IMP (an independent component of
flux-security), and one for Flux running as the system instance.  Each
configuration space has a separate directory, from which all files matching
the glob ``*.toml`` are read.  System administrators have the option of using
one file for each configuration space, or breaking up each configuration space
into multiple files.  In the examples below, one file per configuration space
is used.

For more information on the three configuration spaces, please refer to
:man5:`flux-config`, :security:man5:`flux-config-security`, and
:security:man5:`flux-config-security-imp`.

Configuring flux-security
=========================

When Flux is built to support multi-user workloads, job requests are signed
using a library provided by the flux-security project.  This library reads
a static configuration from ``/etc/flux/security/conf.d/*.toml``. Note
that for security, these files and their parent directory should be owned
by ``root`` without write access to other users, so adjust permissions
accordingly.

Example file installed path: ``/etc/flux/security/conf.d/security.toml``

.. code-block:: toml

 # Job requests should be valid for 2 weeks
 # Use munge as the job request signing mechanism
 [sign]
 max-ttl = 1209600  # 2 weeks
 default-type = "munge"
 allowed-types = [ "munge" ]

See also: :security:man5:`flux-config-security-sign`.

Configuring the IMP
===================

The Independent Minister of Privilege (IMP) is the only program that runs
as root, by way of the setuid mode bit.  To enhance security, it has a
private configuration space in ``/etc/flux/imp/conf.d/*.toml``. Note that
the IMP will verify that files in this path and their parent directories
are owned by ``root`` without write access from other users, so adjust
permissions and ownership accordingly.

Example file installed path: ``/etc/flux/imp/conf.d/imp.toml``

.. code-block:: toml

 # Only allow access to the IMP exec method by the 'flux' user.
 # Only allow the installed version of flux-shell(1) to be executed.
 [exec]
 allowed-users = [ "flux" ]
 allowed-shells = [ "/usr/libexec/flux/flux-shell" ]

 # Enable the "flux" PAM stack (requires PAM configuration file)
 pam-support = true

See also: :security:man5:`flux-config-security-imp`.

Configuring the Flux PAM Stack
------------------------------

If PAM support is enabled in the IMP config, the ``flux`` PAM stack must
exist and have at least one ``auth`` and one ``session`` module.

Example file installed path: ``/etc/pam.d/flux``

.. code-block:: console

  auth    required pam_localuser.so
  session required pam_limits.so

The ``pam_limits.so`` module is useful for setting default job resource
limits.  If it is not used, jobs run in the system instance may inherit
inappropriate limits from ``flux-broker``.

.. note::
  The linux kernel employs a heuristic when assigning initial limits to
  pid 1.  For example, the max user processes and max pending signals
  are scaled by the amount of system RAM.  The Flux system broker inherits
  these limits and passes them on to jobs if PAM limits are not configured.
  This may result in rlimit warning messages similar to

  .. code-block:: console

    flux-shell[0]:  WARN: rlimit: nproc exceeds current max, raising value to hard limit

.. _config_cert:

Configuring the Network Certificate
===================================

Overlay network security requires a certificate to be distributed to all nodes.
It should be readable only by the ``flux`` user.  To create a new certificate,
run :man1:`flux-keygen` as the ``flux`` user, then copy the result to
``/etc/flux/system`` since the ``flux`` user will not have write access to
this location:

.. code-block:: console

 $ sudo -u flux flux keygen /tmp/curve.cert
 $ sudo mv /tmp/curve.cert /etc/flux/system/curve.cert

Do this once and then copy the certificate to the same location on
the other nodes, preserving owner and mode.

.. note::
    The ``flux`` user only needs read access to the certificate and
    other files and directories under ``/etc/flux``. Keeping these files
    and directories non-writable by user ``flux`` adds an extra layer of
    security for the system instance configuration.

Systemd and cgroup unified hierarchy
====================================

The flux systemd unit launches a systemd user instance as the flux user.
It is recommended to use this to run user jobs, as it provides cgroups
containment and the ability to enforce memory limits.  To do this, Flux
requires the cgroup version 2 unified hierarchy:

- The cgroup2 file system must be mounted on  ``/sys/fs/cgroup``

- On some systems, add ``systemd.unified_cgroup_hierarchy=1`` to the
  kernel command line (RHEL 8).

- On some systems, add ``cgroup_enable=memory`` to the kernel command line
  (debian 12).

The configuration that follows presumes jobs will be launched through systemd,
although it is not strictly required if your system cannot meet these
prerequisites.

.. _config-flux:

Configuring the Flux System Instance
====================================

Although the security components need to be isolated, most Flux components
share a common configuration space, which for the system instance is located
in ``/etc/flux/system/conf.d/*.toml``.  The Flux broker for the system instance
is pointed to this configuration by the systemd unit file.

Example file installed path: ``/etc/flux/system/conf.d/system.toml``

.. code-block:: toml

 # Enable the sdbus and sdexec broker modules
 [systemd]
 enable = true

 # Flux needs to know the path to the IMP executable
 [exec]
 imp = "/usr/libexec/flux/flux-imp"

 # Run jobs in a systemd user instance
 service = "sdexec"

 # Limit jobs to a percentage of physical memory
 [exec.sdexec-properties]
 MemoryMax = "95%"

 # Allow users other than the instance owner (guests) to connect to Flux
 # Optionally, root may be given "owner privileges" for convenience
 [access]
 allow-guest-user = true
 allow-root-owner = true

 # Point to shared network certificate generated flux-keygen(1).
 # Define the network endpoints for Flux's tree based overlay network
 # and inform Flux of the hostnames that will start flux-broker(1).
 [bootstrap]
 curve_cert = "/etc/flux/system/curve.cert"

 default_port = 8050
 default_bind = "tcp://eth0:%p"
 default_connect = "tcp://%h:%p"

 # Rank 0 is the TBON parent of all brokers unless explicitly set with
 # parent directives.
 hosts = [
    { host = "test[1-16]" },
 ]

 # Speed up detection of crashed network peers (system default is around 20m)
 [tbon]
 tcp_user_timeout = "2m"

 # Uncomment 'norestrict' if flux broker is constrained to system cores by
 # systemd or other site policy.  This allows jobs to run on assigned cores.
 # Uncomment 'exclude' to avoid scheduling jobs on certain nodes (e.g. login,
 # management, or service nodes).
 [resource]
 #norestrict = true
 #exclude = "test[1-2]"

 [[resource.config]]
 hosts = "test[1-15]"
 cores = "0-7"
 gpus = "0"

 [[resource.config]]
 hosts = "test16"
 cores = "0-63"
 gpus = "0-1"
 properties = ["fatnode"]

 # Store the kvs root hash in sqlite periodically in case of broker crash.
 # Recommend offline KVS garbage collection when commit threshold is reached.
 [kvs]
 checkpoint-period = "30m"
 gc-threshold = 100000

 # Immediately reject jobs with invalid jobspec or unsatisfiable resources
 [ingest.validator]
 plugins = [ "jobspec", "feasibility" ]

 # Remove inactive jobs from the KVS after one week.
 [job-manager]
 inactive-age-limit = "7d"

 # Jobs submitted without duration get a very short one
 [policy.jobspec.defaults.system]
 duration = "1m"

 # Jobs that explicitly request more than the following limits are rejected
 [policy.limits]
 duration = "2h"
 job-size.max.nnodes = 8
 job-size.max.ncores = 32

 # Configure the flux-sched (fluxion) scheduler policies
 # The 'lonodex' match policy selects node-exclusive scheduling, and can be
 # commented out if jobs may share nodes.
 [sched-fluxion-qmanager]
 queue-policy = "easy"
 [sched-fluxion-resource]
 match-policy = "lonodex"
 match-format = "rv1_nosched"

See also: :man5:`flux-config-exec`, :man5:`flux-config-access`
:man5:`flux-config-bootstrap`, :man5:`flux-config-tbon`,
:man5:`flux-config-resource`, :man5:`flux-config-ingest`,
:man5:`flux-config-job-manager`,
:man5:`flux-config-policy`, :man5:`flux-config-kvs`,
:man5:`flux-config-systemd`,
:sched:man5:`flux-config-sched-fluxion-qmanager`,
:sched:man5:`flux-config-sched-fluxion-resource`.


Configuring Resources
=====================

The Flux system instance must be configured with a static resource set.
The ``resource.config`` TOML array in the example above is the preferred
way to configure clusters with a resource set consisting of only nodes,
cores, and GPUs.

More complex resource sets may be represented by generating a file in
RFC 20 (R version 1) form with scheduler extensions using a combination of
``flux R encode`` and ``flux ion-R encode`` and then configuring
``resource.path`` to its fully-qualified file path.  The details of this
method are beyond the scope of this document.

When Flux is running, ``flux resource list`` shows the configured resource
set and any resource properties.

Persistent Storage on Rank 0
============================

Flux is prolific in its use of disk space to back up its key value store,
proportional to the number of jobs run and the quantity of standard I/O.
On your rank 0 node, ensure that the ``statedir`` directory (normally
``/var/lib/flux``) has plenty of space and is preserved across Flux instance
restarts.

The ``statedir`` directory is used for the ``content.sqlite`` file that
contains content addressable storage backing the Flux key value store (KVS).

Adding Prolog/Epilog/Housekeeping Scripts
=========================================

Flux can execute site-defined scripts as root on compute nodes before and
after each job.

prolog
  The prolog runs as soon as the job enters RUN state.  Job shells are not
  launched until all prolog tasks have completed.  If the prolog fails on
  any nodes, or if any node takes longer than a fail-safe timeout (default
  30m), those nodes are drained and a fatal exception is raised on the job.
  If the job is canceled or reaches its time limit during the prolog, the
  prolog is simply aborted and the job enters COMPLETING state.

epilog
  The epilog runs after job shell exits on all nodes, with the job held
  in COMPLETING state until all epilog tasks have terminated.  If the epilog
  fails on any nodes, those nodes are drained and a fatal exception is raised
  on the job.  There is no default epilog timeout.

housekeeping
  Housekeeping runs after the job has reached the INACTIVE state.  It is not
  recorded in the job eventlog and does not affect the job result.  If
  housekeeping fails on any nodes, those nodes are drained.  Housekeeping
  releases resources to the scheduler as they complete.

Script Installation Locations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. note::
   New in v0.78.0

When configured as recommended below, Flux runs prolog, epilog and
housekeeping scripts from the following locations (in order):

 - Package provided scripts in ``$libexecdir/flux/{name}.d``, where `{name}`
   is `prolog`, `epilog`, or `housekeeping`.

 - If ``/etc/flux/system/{name}`` exists and is executable, then this
   site provided script is run next. This provides backward compatible support
   for existing installations or allows sites to override default behavior
   for execution of site-provided scripts.

 - If ``/etc/flux/system/{name}`` does not exist or is not executable, then
   all scripts in ``/etc/flux/system/{name}.d`` are executed.

.. note::
   The location of ``$libexecdir`` is system dependent, but can be determined
   from :command:`pkg-config --variable=fluxlibexecdir flux-core`.

Script Environment
~~~~~~~~~~~~~~~~~~

Scripts may use :envvar:`FLUX_JOB_ID` and :envvar:`FLUX_JOB_USERID` to
take job or user specific actions.  Flux commands can be run from the
scripts with instance owner credentials if the system is configured for
root access as suggested in :ref:`config-flux`.

The IMP sets :envvar:`PATH` to a safe ``/usr/sbin:/usr/bin:/sbin:/bin``.

Error Handling
~~~~~~~~~~~~~~

By default, the Flux prolog, epilog, and housekeeping collect exit codes from
all scripts and will exit with the highest exit code encountered. This allows
all scripts to run even if some fail.

To change this behavior and exit immediately on the first script failure,
one or more of the following entries can be added to the broker configuration:

.. code-block:: toml

   [job-manager.prolog]
   exit-on-first-error = true

   [job-manager.epilog]
   exit-on-first-error = true

   [job-manager.housekeeping]
   exit-on-first-error = true

Configuration
~~~~~~~~~~~~~

 1. Flux provides systemd *oneshot* units ``flux-prolog@``, ``flux-epilog@``,
    and ``flux-housekeeping@`` templated by jobid, which run the actual
    workflow described in the sections above.  Configure the IMP to allow the
    system instance user to start these units as root via the provided
    provided wrapper scripts:

    .. code-block:: toml

       [run.prolog]
       allowed-users = [ "flux" ]
       allowed-environment = [ "FLUX_*" ]
       path = "/usr/libexec/flux/cmd/flux-run-prolog"

       [run.epilog]
       allowed-users = [ "flux" ]
       allowed-environment = [ "FLUX_*" ]
       path = "/usr/libexec/flux/cmd/flux-run-epilog"

       [run.housekeeping]
       allowed-users = [ "flux" ]
       allowed-environment = [ "FLUX_*" ]
       path = "/usr/libexec/flux/cmd/flux-run-housekeeping"


 2. Configure the Flux system instance to run prolog, epilog, and housekeeping:

    .. code-block:: toml

       [job-manager]
       plugins = [
         { load = "perilog.so" }
       ]

       [job-manager.prolog]
       per-rank = true
       # timeout = "30m"

       [job-manager.epilog]
       per-rank = true
       # timeout = "0"

       [job-manager.housekeeping]
       release-after = "30s"

Standard output and standard error of the prolog, epilog, and housekeeping
units are captured by the systemd journal.  Standard systemd tools like
:linux:man1:`systemctl` and :linux:man1:`journalctl` can be used to
observe and manipulate the prolog, epilog, and housekeeping systemd units.

See also:
:man1:`flux-housekeeping`.
:man5:`flux-config-job-manager`,
:security:man5:`flux-config-security-imp`,

Adding Job Request Validation
=============================

Jobs are submitted to Flux via a job-ingest service. This service
validates all jobs before they are assigned a jobid and announced to
the job manager. By default, only basic validation is done, but the
validator supports plugins so that job ingest validation is configurable.

The list of available plugins can be queried via
``flux job-validator --list-plugins``. The current list of plugins
distributed with Flux is shown below:

.. code-block:: console

  $ flux job-validator --list-plugins
  Available plugins:
  feasibility           Use feasibility service to validate job
  jobspec               Python bindings based jobspec validator
  require-instance      Require that all jobs are new instances of Flux
  schema                Validate jobspec using jsonschema

Only the ``jobspec`` plugin is enabled by default.

In a system instance, it may be useful to also enable the ``feasibility`` and
``require-instance`` validators.  This can be done by configuring the Flux
system instance via the ``ingest`` TOML table, as shown in the example below:

.. code-block:: toml

  [ingest.validator]
  plugins = [ "jobspec", "feasibility", "require-instance" ]

The ``feasibility`` plugin will allow the scheduler to reject jobs that
are not feasible given the current resource configuration. Otherwise, these
jobs are enqueued, but will have a job exception raised once the job is
considered for scheduling.

The ``require-instance`` plugin rejects jobs that do not start another
instance of Flux. That is, jobs are required to be submitted via tools
like :man1:`flux-batch` and :man1:`flux-alloc`, or the equivalent.
For example, with this plugin enabled, a user running :man1:`flux-run`
will have their job rejected with the message:

.. code-block:: console

  $ flux run -n 1000 myapp
  flux-run: ERROR: [Errno 22] Direct job submission is disabled for this instance. Please use the flux-batch(1) or flux-alloc(1) commands.

See also: :man5:`flux-config-ingest`.

Adding Queues
=============

It may be useful to configure a Flux system instance with multiple queues.
Each queue should be associated with a non-overlapping resource subset,
identified by property name. It is good practice for queues to create a
new property that has the same name as the queue. (There is no requirement
that queue properties match the queue name, but this will cause extra entries
in the PROPERTIES column for these queues. When queue names match property
names, :command:`flux resource list` suppresses these matching properties
in its output.)

When queues are defined, all jobs must specify a queue at submission time.
If that is inconvenient, then ``policy.jobspec.defaults.system.queue`` may
define a default queue.

Finally, queues can override the ``[policy]`` table on a per queue basis.
This is useful for setting queue-specific limits.

Here is an example that puts these concepts together:

.. code-block:: toml

 [policy]
 jobspec.defaults.system.duration = "1m"
 jobspec.defaults.system.queue = "debug"

 [[resource.config]]
 hosts = "test[1-4]"
 properties = ["debug"]

 [[resource.config]]
 hosts = "test[5-16]"
 properties = ["batch"]

 [queues.debug]
 requires = ["debug"]
 policy.limits.duration = "30m"

 [queues.batch]
 requires = ["batch"]
 policy.limits.duration = "4h"

When named queues are configured, :man1:`flux-queue` may be used to
list them:

.. code-block:: console

 $ flux queue status
 batch: Job submission is enabled
 debug: Job submission is enabled
 Scheduling is enabled

See also: :man5:`flux-config-policy`, :man5:`flux-config-queues`,
:man5:`flux-config-resource`, :man1:`flux-queue`.

Policy Limits
=============

Job duration and size are unlimited by default, or limited by the scheduler
feasibility check discussed above, if configured.  When policy limits are
configured, the job request is compared against them *after* any configured
jobspec defaults are set, and *before* the scheduler feasibility check.
If the job would exceed a duration or job size policy limit, the job submission
is rejected.

.. warning::
  flux-sched 0.25.0 limitation: jobs that specify nodes but not cores may
  escape flux-core's ``ncores`` policy limit, and jobs that specify cores but
  not nodes may escape the ``nnodes`` policy limit.  The flux-sched feasibility
  check will eventually cover this case.  Until then, be sure to set both
  ``nnodes`` *and* ``ncores`` limits when configuring job size policy limits.

Limits are global when set in the top level ``[policy]`` table.  Global limits
may be overridden by a ``policy`` table within a ``[queues]`` entry.  Here is
an example which implements duration and job size limits for two queues:

.. code-block:: toml

 # Global defaults
 [policy]
 jobspec.defaults.system.duration = "1m"
 jobspec.defaults.system.queue = "debug"

 [queues.debug]
 requires = ["debug"]
 policy.limits.duration = "30m"
 policy.limits.job-size.max.nnodes = 2
 policy.limits.job-size.max.ncores = 16

 [queues.batch]
 requires = ["batch"]
 policy.limits.duration = "8h"
 policy.limits.job-size.max.nnodes = 16
 policy.limits.job-size.max.ncores = 128

See also: :man5:`flux-config-policy`.

Use PAM to Restrict Access to Compute Nodes
===========================================

If Pluggable Authentication Modules (PAM) are in use within a cluster, it may
be convenient to use the ``pam_flux.so`` *account* module to configure a PAM
stack that denies users access to compute nodes unless they have a job running
there.

Install the ``flux-pam`` package to make the ``pam_flux.so`` module available
to be added to one or more PAM stacks, e.g.

.. code-block:: console

  account  sufficient pam_flux.so
