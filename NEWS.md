flux-core version 0.36.0 - 2022-03-01
-------------------------------------

This release adds support for restarting a Flux system instance in safe
mode after a failure to shut down properly -- for example in the case of a
broker crash.  New `flux-startlog(1)` and `flux-uptime(1)` commands are
also introduced to give a quick review of the start and stop times and
status of the current Flux instance.

System instance users will want to update their configuration files to
set `tbon.tcp_user_timeout` and remove `tbon.keepalive_*`, if present.
For more information, see the Flux Admin Guide:

https://flux-framework.readthedocs.io/en/latest/adminguide.html

### Fixes

 * job-exec: fix job hang after early IMP/shell exit (#4155)
 * broker: allow `tbon.zmqdebug` to be set in config file and make sure it's
   really off if set to 0 (#4127)
 * broker: handle network partition (#4130)
 * shell: capture job shell error messages in designated output file (#4125)
 * resource: emit a more specific error when `rlist_rerank()` fails (#4126)
 * flux-overlay: fix timeout error message (#4131)
 * README: add libc development packages in requirements (#4133)
 * libflux/future: set missing errno in `flux_future_wait_for()`  (#4162)
 * flux-config-archive(5): fix TOML example (#4164)
 * shell: fix delay in completion of jobs with a single shell rank (#4159)

### New Features

 * flux-uptime: provide useful output for slow/stuck broker state (#4172)
 * improve KVS checkpoint protocol to allow for future changes (#4149)
 * add `flux config get` (#4166)
 * broker: use RPC not control message for broker module sync/status (#4110)
 * docs: add Python overview documentation (#4104)
 * Support new libsdprocess to launch processes under systemd (#3864)
 * rename keepalive messages to control messages (#4112)
 * resource: enhance resource.drain RPC with "update" and "overwrite" modes
   (#4121)
 * broker: replace keepalive tunables with `tcp_user_timeout` (#4118)
 * kvs: add date to kvs-primary checkpoint (#4136)
 * libpmi2: implement bits needed for Cray MPI (#4142)
 * add `flux-uptime` command (#4148)
 * add `flux-startlog` and enter safe mode after crash (#4153)
 * libflux: add `flux_hostmap_lookup(3)` (#4157)

### Cleanup

 * drop unused project meta files (#4170)
 * doc: update flux-broker-attributes(7) (#4119)
 * python: return `JobID` from flux.job.submit, not `int` (#4134)
 * consolidate multiple `*_error_t` structures into a common `flux_error_t`
   (#4165)
 * drop unused project meta files (#4170)

### Testsuite

 * testsuite: remove unportable cshism (#4115)
 * codecov: minor improvements for coverage reporting (#4147)
 * testsuite: add clarification comments (#4167)


flux-core version 0.35.0 - 2022-02-05
-------------------------------------

This release fixes a broker crash when a job receives an exception after
running a job prolog. Users of the prolog/epilog feature should update
to this version as soon as possible.

In addition, TCP keepalive support was added for detection of powered off
compute nodes.  For configuration info, see the Flux Admin Guide:

https://flux-framework.readthedocs.io/en/latest/adminguide.html


### Fixes

 * flux-ping: support hostnames in TARGET #4105
 * Fix broker segfault when an exception is raised on a job after prolog (#4096)
 * flux-overlay: improve timeouts, hostname handling (#4095)
 * flux resource: allow limited guest access (#4094)
 * shell: fix duplicate logging after evlog plugin is initialized (#4085)
 * shell: do not allow instance owner to use guest shell services (#4101)
 * shell: fix race in attach to interactive job pty (#4102)
 * libterminus: fix leak in pty client code (#4098)

### New Features

 * broker: use TCP keepalives (#4099)
 * systemd: set restart=always (#4100)
 * flux-mini: add --wait-event option to submit/bulksubmit (#4078)

### Testsuite

 * testsuite: fix spellcheck (#4082)
 * ci: rename centos images to el, and use rockylinux for el8 image (#4080)


flux-core version 0.34.0 - 2022-01-28
-------------------------------------

This release features the automatic draining of "torpid" (unresponsive)
nodes, to prevent new work from being scheduled on them until the instance
owner investigates and runs `flux resource undrain`.

### Fixes

 * libsubprocess: fix excess logging and logging corner cases (#4060)
 * doc: fix cross-references (#4063)
 * flux-proxy: improve experience when proxied Flux instance terminates
   (#4058)
 * flux-perilog-run: improve usefulness of logging when prolog/epilog fails
   (#4054)
 * Fix issues found on Cray Shasta (perlmutter) (#4050)
 * env: fix prepend of colon-separated paths in reverse order (#4045)
 * python: fix ImportError for collections.abc.Mapping (#4042)
 * job-list: fix "duplicate event" errors (#4043)
 * systemd: set linger on flux user (#4035)

### New Features

 * shell: enhance pty support (#4075)
 * add broker.starttime; add uptime to flux-top, flux-pstree (#4076)
 * libflux: add `flux_reconnect()`, revamp flux fatal error callback (#4016)
 * doc: add/improve man pages for config files (#4057, #4069)
 * resource: drain torpid nodes (#4052)

### Cleanup

 * broker/content: misc cleanup (#4074)
 * improve error message from flux-proxy and flux-jobs for invalid and
   unknown jobids (#4062)
 * cmd/flux-ping: make help output clearer (#4061)
 * configure: Add python docutils check, do not require doc utils to build
   flux help (#4056)

### Test

 * testsuite: fix non-bourne shell test failure (#4064)
 * sharness: unset `FLUX_CONF_DIR` for all tests (#4059)
 * ci: fix use of poison-libflux.sh and add poison `flux-*` commands (#4046)


flux-core version 0.33.0 - 2022-01-08
-------------------------------------

This release includes several improvements in the recursive tooling
in Flux to enhance the user experience when dealing with nested jobs.

Highlights include:

 * Improved interface for job URI discovery allows `flux proxy` and
   `flux top` to target jobids directly.
 * Addition of a `-R, --recursive` option to `flux jobs`
 * Support in `flux top` for selecting and recursing into sub-instances
 * A new  `flux pstree` command for displaying job hierarchies in a tree

### Fixes

 * systemd: fix typo in flux.service unit file (#3996)
 * libflux: check reactor flags (#4014)
 * fix uninterruptible hang when attached to terminated jobs with -o pty
   (#4010)
 * cmd/flux-jobs: re-work -A option and clarify -a option (#4012)
 * broker: avoid inappropriate quorum.timeout (#4027)
 * add workaround for invalid job timeouts when system is busy (#4037)

### New Features

 * add FluxURIResolver Python class and flux-uri command for job URI
   discovery (#3999)
 * cmd: support high-level URIs and JOBID arguments in flux-top and
   flux-proxy (#4004, #4015)
 * flux-top: allow top to recursively call itself (#4011)
 * flux-jobs: add --recursive option (#4019, #4024)
 * flux-jobs: support instance-specific fields in output (#4022)
 * add flux-pstree command (#4026)

### Cleanup

 * doc: add flux-resource(1),  clean up help output (#4021)
 * doc: audit / cleanup SEE ALSO and RESOURCES, add cross references (#4007)
 * doc: misc updates and fixes (#4009)
 * connector cleanup (#4013)
 * connectors: avoid embedded synchronous RPC for subscribe/unsubscribe
   (#3997)

### Test

 * testsuite: minor testsuite fixes (#4023)
 * ci: add ability to run tests under system instance (#3844)
 * fluxorama: allow user to sudo to flux user, add common systemd environment
   vars to flux user's bashrc (#4031)


flux-core version 0.32.0 - 2021-12-05
-------------------------------------

This release adds early groundwork for recovering running jobs across a
Flux restart.  It also includes improved log messages based on user feedback
about Flux failures on real workflow runs, a first draft of a new `flux top`
tool, and a critical fix for system deployments of Flux (#3958).

### Fixes

 * python: fix reference counting for Python Message objects (#3983)
 * python: avoid early garbage collection of Watcher objects (#3975)
 * libflux: improve safety against refcounting bugs in message functions (#3985)
 * shell: reject PMI clients that request v2 (#3953)
 * resource: don't abort if topo-reduce is received more than once (#3958)

### New Features

 * systemd: start flux systemd user service (#3872)
 * broker: record child instance URIs as job memo (#3986)
 * Support job memo events (#3984)
 * job-exec: checkpoint/restore KVS namespaces of running jobs (#3947)
 * set hostlist broker attribute when boostrapped by PMI (#3966)
 * add `flux_get_hostbyrank()` and improve broker attribute caching (#3971)
 * broker: log slow nodes during startup (#3980)
 * add flux-top command (#3979)

### Cleanup

 * flux-overlay: improve default status output, rework options (#3974)
 * job-exec: improve job exception message/logging on broker disconnect (#3962)
 * drop flux-jobspec command (#3951)
 * improve flux-mini bulksubmit --dry-run output with --cc (#3956)
 * README: update LLNL-CODE (#3954)
 * broker/overlay: misc cleanup (#3948)
 * bring README.md up to date (#3990)
 * docker: fix and update ci dockerfiles (#3991)

### Test

 * testsuite: sanitize environment, fix hang in t2607-job-shell-input.t (#3968)

flux-core version 0.31.0 - 2021-11-05
-------------------------------------

This release includes two noteworthy system instance improvements:
crashed/offline nodes now marked offline for scheduling, and support
has been added for prolog/epilog scripts that run as root.

For prolog/epilog setup info, see the Flux Admin Guide:

https://flux-framework.readthedocs.io/en/latest/adminguide.html

### Fixes
 * build: allow python 3.10.0 to satisfy version check (#3939)
 * resource: avoid scheduling on nodes that have crashed (#3930)
 * broker: fail gracefully when rundir or local-uri exceed `AF_UNIX` path
   limits (#3932)
 * broker: ignore missing ldconfig when searching for libpmi.so (#3926)
 * job-manager: fix running job count underflow and use-after-free when an
   event is emitted in CLEANUP state (#3922)
 * fix problems building flux when 0MQ is not installed as a system package
   (#3917)
 * python: do not auto-stop ProgressBar by default (#3914)

### New Features
 * support job prolog/epilog commands (#3934)
 * job-manager: add prolog/epilog support for jobtap plugins (#3924)
 * libidset: add high level set functions (#3915)
 * kvs: optionally initialize namespace to a root reference (#3941)
 * rc: load job-archive module in default rc (#3942)

### Cleanup
 * improve broker overlay logging and debugging capability (#3913)
 * man: Add note about shell quoting/escaping (#3918)

### Test
 * mergify: set queue method to merge, not rebase (#3916)
 * mergify: replace strict merge with queue+rebase (#3907)
 * testsuite: fix non-bourne shell test failure (#3937)

flux-core version 0.30.0 - 2021-10-06
-------------------------------------

### Fixes

 * job-manager: small fixes for the alloc-bypass plugin (#3889)
 * job-manager: release after:JOBID dependencies after "start" instead of
   "alloc" event (#3865)
 * shell: avoid dropping stderr after a PMI abort (#3898)
 * shell: require `FLUX_SHELL_PLUGIN_NAME` in plugins to fix logging component
   discovery (#3879)
 * libflux: deactivate RPC message handlers after final response (#3853)
 * remove duplicate directories from `FLUX_RC_EXTRA`, `FLUX_SHELL_RC_PATH`
   (#3878)
 * t: fix incorrect method call in test-terminal.perl (#3888)
 * Fix a couple build and test issues on ppc64le with clang 6.0+ (#3875)

### New Features

 * jobtap: allow jobtap plugins to query posted events for jobs (#3863)
 * jobtap: allow jobtap plugins to subscribe to job events (#3861)
 * job-exec: enable manual override option for mock execution jobs (#3868)
 * shell: improve initrc extensibility, support version specific mpi plugin
   loading (#3890)
 * shell: fixes and enhancements for plugin loading (#3859)
 * shell: allow default rc path to be extended via `FLUX_SHELL_RC_PATH` (#3869)
 * shell: add taskids idset to `flux_shell_get_rank_info(3)` (#3873)
 * shell: add library of Lua convenience functions for use in plugins (#3856)
 * resource: fail get-xml request on quorum subset (#3885)

### Cleanup

 * libflux/future: fix comment typo (#3860)
 * NEWS.md: Fix typo for v0.29.0 (#3857)

### Testsuite

 * docker: add --build-arg to docker-run-checks, systemd-devel to centos8
   (#3871)
 * ci: add fedora 34 build and fix compiler errors from gcc 11.2 (#3854)


flux-core version 0.29.0 - 2021-09-03
-------------------------------------

This release of Flux includes a new fault mechanism which ensures that
unanswered RPCs receive error responses when the overlay network is
disrupted. Also included is a new `flux overlay` utility which can be
used to manage and report the status of the overlay network.

### Fixes
 * shell: fix in-tree pluginpath, add `shell_plugindir` (#3841)
 * python: fix bug in FluxExecutor.attach method (#3839)
 * rlist: fix erroneous collapse of nodes with different resource children
   when emitting R (#3814)
 * libkvs: compact only if ops len > 1 (#3807)
 * python: allow executor to attach to jobs (#3790)
 * python: fix version requirement in jobspec validator plugin (#3784)
 * broker: ensure subtree restart upon loss of router node (#3845)
 * broker: drop -k-ary option, rename tbon.arity attr to tbon.fanout (#3796)
 * add flux-job(1) manual page, plus minor fixes (#3763)
 * libjob: improve method for determining instance owner (#3761)

### New Features
 * add flux overlay status command (#3816)
 * broker: improve logging of 0MQ socket events (#3846)
 * broker: fail pending RPCs when TBON parent goes down (#3843)
 * broker: fail pending RPCs when TBON child goes down (#3822)
 * add `bootstrap.ipv6_enable = true` config option (#3827)
 * shell: add functions to access jobspec summary information (#3835)
 * add stats api for internal metric collection (#3806, #3824)
 * support io encode/decode of binary data (#3778)
 * add flag to bypass jobspec validation (#3766)
 * libflux: add time stamp to message trace (#3765)

### Cleanup
 * libzmqutil: generalize the zeromq authentication protocol server (#3847)
 * libflux: use iovec-like array over zmsg (#3773)
 * libflux: update flux msg route functions (#3746)
 * libflux: message API fixes and cleanup (#3771)
 * libjob: break up job.c (#3768)
 * build: consistently use CFLAGS / LIBS in Makefiles (#3785)
 * use CCAN base64 library over libsodium base64 library (#3789)
 * drop unnecessary 0MQ includes (#3782)
 * various other cleanup (#3762)

### Testsuite
 * udate to flux-security v0.5.0 in docker images (#3849)
 * make valgrind test opt-in (#3840)
 * add valgrind suppression for opencl and libev on aarch64 (#3794, #3809)

flux-core version 0.28.0 - 2021-06-30
-------------------------------------

This release adds simple job dependencies - see the `flux_mini(1)`
DEPENDENCIES section.

### Fixes
 * shell: fix segfault when more slots are allocated than requested (#3749)
 * testsuite: avoid long `ipc://` paths in system test personality (#3739)
 * cron: fix race in task timeouts (#3728)
 * Python/FluxExecutor bug fixes (#3714)
 * flux-python: fix use of virtualenv python (#3713)
 * optparse: make `optional_argument` apply to long options only (#3706)
 * librlist: skip loading hwloc 'gl' plugin (#3693)

### New Features
 * allow jobs to bypass scheduler with alloc-bypass jobtap plugin (#3740)
 * libjob: add a library for constructing V1 jobspecs (#3662, #3734, #3748)
 * python: validate dependencies in Jobspec constructor (#3727)
 * libflux: make `flux_plugin_handler` topic member const (#3720)
 * job-manager: add builtin begin-time dependency plugin (#3704)
 * broker: send offline responses while broker is initializing (#3712)
 * python: add `flux.util.parse_datetime` (#3711)
 * job-manager: support simple `after*` job dependencies (#3696)
 * jobtap: fixes and api enhancements to support dependency plugins (#3698)
 * shell: add exit-on-error option (#3692)

### Cleanup/Testing/Build System
 * job-manager: minor cleanup and improvements for event handling (#3759)
 * libflux: make `flux_msg_fprint()` output clearer (#3742)
 * libflux: store fully decoded message in `flux_msg_t` (#3701, #3758)
 * libflux: msg API cleanup, test cleanup, and test enhancement  (#3745, #3699)
 * testsuite: generalize valgrind suppressions (#3743)
 * ci: use long path for builddir in test build (#3738)
 * job-list: cleanup & testsuite modernization & consistency updates (#3733)
 * testsuite: fix several tests on slower systems (#3730)
 * testsuite: fix intermittent test, speed up others (#3725)
 * broker: misc cleanup (#3721)
 * github: fix ci builds on master (#3716, #3717)
 * testsuite: add tests for late joining broker (#3709)
 * flux-start: build system instance test features (#3700)
 * ci: minor coverage testing fixes (#3703)
 * libflux: test: fix segfault of `test_plugin.t` under rpmbuild (#3695)

flux-core version 0.27.0 - 2021-05-28
-------------------------------------

This release features additonal performance improvements that affect
job throughput over time (see issue #3583).

### Fixes
 * shell/pmi: always populate `PMI_process_mapping` to avoid mvapich2
   `MPI_Init` invalid tag error (#3673)
 * openmpi: ensure that shmem segments for co-located jobs don't conflict
   (#3672)
 * python: fix FluxExecutorFuture cancellation bug (#3655)
 * job-info, kvs-watch: support guest disconnect & credential checks (#3627)
 * libflux: plugin: make `FLUX_PLUGIN_ARG_UPDATE` the default (#3685)

### Performance
 * kvs: reduce cache expiration overhead (#3664)
 * kvs: remove client disconnect bottleneck (#3663)
 * kvs: use json object to find duplicate keys (#3658)
 * kvs: improve performance of transaction prep/check (#3654)
 * content-cache: avoid linear search for dirty blobs (#3639)
 * content-cache: make LRU purge more effective (#3632)
 * flux-shell: add minor optimizations for single node jobs (#3626)
 * libczmqcontainers: include zlistx, zhash, zlist, and convert internal
   users (#3620)

### New Features
 * shell: add plugin to detect first task exit (#3681)
 * job-manager: multiple jobtap plugin enhancements (#3687)
 * job-manager: support a list of loaded jobtap plugins (#3667)
 * shell: add tmpdir plugin to manage `FLUX_JOB_TMPDIR` (#3661)
 * jobtap: support for `job.dependency.*` callbacks (#3660)
 * flux-mini: avoid substitution without --cc/bcc, allow --setattr value
   to be read from file (#3659)
 * flux-start: add embedded server (#3650)
 * flux-proxy: add flux-core version check (#3653)
 * libflux: `msg_handler`: capture duplicate non-glob request handlers in a
   stack (#3616)

### Cleanup/Testing/Build System
 * testsuite: add mvapich2 to centos8 CI image (#3686)
 * testsuite: improve in-tree MPI testing (#3678)
 * libflux: `flux_modfind`: ignore DSOs with no `mod_name` symbol (#3675)
 * kvs: misc cleanup (#3671)
 * flux-start: rename `--scratchdir` to `--rundir` (#3670)
 * shell: misc environment-related fixes (#3669)
 * testsuite: modify jobid capture logic (#3657)
 * testsuite: handle hwloc issues and improve config file bootstrap test
   (#3648)
 * build: add and use autoconf variable for Flux plugin LDFLAGS (#3647)
 * libutil: replace hand written hex conversion code with libccan (#3646)
 * github: fixes for auto-release deployment (#3638)
 * content-cache: general cleanup, small bug fixes, and test improvement
   (#3645)
 * kvs: add errmsg on namespace create failure (#3644)
 * Use internal functions instead of zfile / zdigest (#3634)
 * libutil: avoid `zmq_strerror()` (#3628)
 * ci/test: switch to bulksubmit for inception tests, add throughput test,
   dismiss reviews after PR updates (#3621)
 * expand broker internal documentation to cover bootstrap phase (#3618)

flux-core version 0.26.0 - 2021-04-22
-------------------------------------

This release features several performance improvements that affect
job throughput over time (see issue #3583).

### Fixes

 * avoid mvapich segfault under flux start singleton (#3603)
 * python: avoid throwing 2nd exception on unknown errno (#3588)
 * avoid routing stale responses to restarted brokers (#3601)

### Performance

 * fix aggressive zhashx resize by importing fixed source (#3596, #3598)
 * use zhashx, LRU in content-cache (#3589, #3593)
 * drop root directory object from KVS setroot event (#3581)
 * add minor optimizations to aux container (#3586)
 * drop extra code in `flux_matchtag_free()` (#3590)
 * libkvs: save KVS copy/move aux data in future not handle (#3585)

### New Features

 * libjob: add `flux_job_result(3)` (#3582)
 * python: add explicit `service_(un)register` method (#3602)
 * add overlay network version/config check (#3597)
 * job-manager: enable job dependency management (#3563)

### Cleanup/Testing

 * flux-start: rename --size to --test-size, drop --bootstrap (#3605)

flux-core version 0.25.0 - 2021-04-01
-------------------------------------

### Fixes

 * kvs: fix assert due to busy KVS (#3560)
 * systemd: configure weak dependency on munge (#3577)
 * Fix various memleaks discovered by ASAN (#3568)
 * README: add missing dependency - pkgconfig (#3570)
 * fix `PMI_process_mapping` for multiple brokers per node (#3553)
 * Python: fix "no such file or directory" job exception resulting from
   bad jobspec (#3534)

### New Features

 * libflux: add `flux_plugin_aux_delete()` (#3565)
 * job-info: support LRU cache mapping job id -> owner (#3548)
 * python: expand FluxExecutor.submit parameters (#3562)
 * broker: add support for PMIx bootstrap (#3537)
 * job-ingest: add new plugin-based job validator (#3533)

### Cleanup/Testing

 * README.md: remove python3-six dependency (#3579)
 * clean up disconnect, cancel handlers (#3569)
 * broker: drop broker.rundir, select ipc vs tcp using broker.mapping (#3554)
 * broker: refactor overlay network send/receive interfaces (#3547)
 * github: add a stale issues and PR bot for flux-core (#3544)
 * build/test: remove stale heartbeat references (#3535)
 * job-info: consolidate watch RPC targets (#3525)
 * enhance testsuite reliability on RHEL8/TOSS4 (#3540)


flux-core version 0.24.0 - 2021-02-22
-------------------------------------

This release features multiple performance enhancements, including the
addition of the FluxExecutor Python class which allows rapid, asynchronous
submission of jobs.

### Fixes

 * broker: fix segfault/perf issues when hitting file descriptor limit (#3513)
 * module: reduce keepalive message traffic (#3516)
 * flux-kvs: fix --help output when not in an instance (#3500)
 * flux-kvs: fix help output in nested subcommands (#3497)
 * flux-mini: fix --progress counters with job exceptions (#3514)
 * portability: fix 32-bit issues (#3507)
 * portability: cross compilation fixes for Julia bindings (#3503)
 * libflux: restart continuation timeout in `flux_future_reset()` (#3518)

### New Features

 * python: add concurrent.futures executor (#3468)
 * libflux: add `flux_sync_create()` (#3524)
 * job-manager: allow jobtap plugins to reject jobs (#3494)
 * job-manager: support mode=limited (#3473)
 * flux-mini: support `--urgency` values "default", "hold", "expedite" (#3499)
 * broker: improve IP address heuristic in PMI bootstrap (#3489)
 * flux-mini: add --log and --log-stderr options (#3509)
 * use reactor time instead of heartbeats for internal time management (#3519)
 * heartbeat: convert to loadable module (#3512)

### Cleanup/Testing

 * job-info: split into two modules, job-info and job-list (#3510)
 * libflux: remove unnecessary `flux_future_then()` calls (#3520)
 * testsuite: cleanup job-manager tests (#3488)
 * testsuite: update hwloc-calc usage (#3523)
 * ci: add fedora33 docker image for testing (#3498)
 * ci: add 32 bit build to github ci checks (#3511)
 * ci: explicitly checkout tag if creating a release (#3531)


flux-core version 0.23.1 - 2021-01-27
-------------------------------------

### Fixes

 * flux resource: allow drain, undrain, and status to work on any rank (#3486)
 * job-manager: fix compilation error on gcc-10 (#3485)
 * job-manager: fix uninitialized variable warning in jobtap.c (#3481)

flux-core version 0.23.0 - 2021-01-25
-------------------------------------

This release adds a job priority plugin framework, enabling the
flux-accounting project to set job priorities with a fair share
algorithm.

The scheduler protocol (RFC 27) and libschedutil convenience API
have changed, therefore users of flux-sched must upgrade to 0.15.0.

### New features

 * jobtap: prototype job-manager plugin support (#3464)
 * flux-mini: add bulk job submission capabilities (#3426, #3478)
 * job-manager: send updated priorities to schedulers (#3442)
 * job-manager: support job hold and expedite (#3428)

### Fixes

 * connectors/ssh: forward `LD_LIBRARY_PATH` over ssh when set (#3458)
 * python: fix use of `Flux.reactor_run()` from multiple threads (#3471)
 * python: misc. fixes to docstrings and argument names in bindings (#3451)
 * python: fix circular reference in `check_future_error` decorator (#3437)
 * python: fix ctrl-c, re-throw unhandled exceptions in `reactor_run()` (#3435)
 * shell: fix dropped stdout from shell plugins in task.exec callback (#3446)

### Cleanup/Testing

 * ci: limit asan build to unit tests only (#3479)
 * libschedutil: API improvements and priority integration (#3447)
 * configure: add switch to allow flux to be built without python (#3459)
 * testsuite: remove sched-dummy, migrate testing to sched-simple (#3462)
 * testsuite: add debug, workarounds for failures in github actions (#3467)
 * test: fix test for installing poison libflux (#3461)
 * cleanup: update outdated terminology (#3456)
 * Globally standardize spelling of "canceled" (#3443)
 * ci: better script portability and other small updates (#3438)
 * testsuite: fix invalid tests, cleanup list-jobs, avoid hard-coding (#3436)
 * fix github actions on tag push (#3430)

flux-core version 0.22.0 - 2020-12-16
-------------------------------------

This release resolves an issue introduced in 0.20.0 where Flux would
occasionally hang during tear-down on RHEL/CentOS 7.  This release
should be suitable for use with production workflows on those systems.

System instance development and testing at < 256 node scale is on-going.
The system limitations of this release are documented in the Flux Admin
Guide:

https://flux-framework.readthedocs.io/en/latest/adminguide.html

### New features

 * flux-keygen is no longer necessary before starting Flux (#3409)
 * Add waitstatus and returncode JobID class and flux-jobs (#3414)
 * New `flux resource status` command (#3351)
 * Rename "administrative priority" to "urgency" (#3394)
 * Prepare for fair share priority plugin (#3371, #3339, #3350, #3402,
   #3405, #3404, #3410)
 * job-manager: cache jobspec for scheduler, exec (#3393, #3396, #3399)
 * python: add bindings for libflux-idset,hostlist (#3341)
 * resource: support hostnames for drain and exclude (#3318)
 * flux-jobs: Support nodelist in flux-jobs output (#3332)
 * flux-jobs: add flux-jobs --stats,--stats-only options (#3419)
 * flux-job: Add flux job attach --read-only option (#3320)
 * python: add ResourceSet python class (#3406)
 * python: allow future.then() variable and keyword args in callbacks (#3366)

### Fixes

 * Fix job shell segfault when jobspec contains JSON null (#3421)
 * job-manager: Fix annotation clear corner case #3418
 * broker: fix intermittent hang during instance tear-down on Centos7 (#3398)
 * job-exec: log early shell/imp errors (#3397)
 * shell: ensure TMPDIR exists for all jobs (#3389)
 * misc cleanups & fixups (#3392)
 * small fixes: resource memory leak, improve errors, check int size (#3388)
 * affinity: use comma separated list format for `CUDA_VISIBLE_DEVICES` (#3376)
 * libjob: repair interoperability with flux-security (#3356)
 * job-exec: fixes for multiuser mode (#3353)
 * shell: fix issues with `CUDA_VISIBLE_DEVICES` value (#3317)
 * job-manager: handle scheduler disconnect (#3304)
 * libjob: always sign owner jobs with the 'none' signing mechanism (#3306)
 * libsubprocess: do not allow ref/unref in hooks (#3303)

### Cleanup/Testing

 * doc: autogenerate python binding docs with Sphinx (#3412)
 * testsuite: support level N inception of flux testsuite (#3413)
 * github: fix missing docker tag in matrix builds (#3387)
 * github: fixes for workflow scripts (#3383)
 * ci: move from Travis-CI to GitHub Workflows (#3379)
 * docs: add explicit link to man pages section (#3365)
 * testsuite: replace loop in t2300-sched-simple.t with helper (#3367)
 * docker: install poison flux-core libs, cmds before build and test (#3369)
 * libflux: drop `flux_dmesg()` from public API (#3362)
 * testsuite: fix shed-simple test races (#3358)
 * build: allow Lua 5.4, drop -lutil, and improve sphinx warning (#3357)
 * testsuite: increase resource.monitor-waitup timeout (#3348)
 * broker: update log.dmesg to use rpc streaming (#3307)
 * testsuite: fix core idsets in resource module tests (#3314)
 * t/t2205-hwloc-basic: only use lstopo-no-graphics (#3309)

flux-core version 0.21.0 - 2020-11-04
-------------------------------------

This release enables resources to be configured in advance when Flux is
the native resource manager for a cluster, in lieu of dynamic discovery.
For details, refer to the Flux Admin Guide:

https://flux-framework.readthedocs.io/en/latest/adminguide.html

### New features

 * optparse: don't sort options/subcommands by default (#3298)
 * flux-job: Output key options for job info (#3210)
 * resource: load resources from config or R, and rework topo discovery (#3265)
 * add internal librlist library and flux-R utility for R version 1 (#3276)
 * job-info: use job manager journal to track job state (#3254)
 * job-manager: support events journal (#3261)
 * shell: support stdio buffering options (default stderr: unbuffered) (#3272)
 * flux-kvs: Add 'flux kvs eventlog wait-event' subcommand (#3200)
 * job-manager: send job annotations to journal instead of publishing (#3236)
 * add hostlist library for encoding/decoding RFC29 hostlists (#3247)

### Fixes

 * broker: convert broker [bootstrap] config to use libhostlist (#3283)
 * libflux: Add missing C++ header guards (#3280)
 * cmd: display jobid with flux-mini alloc -v, --verbose (#3279)
 * python: fix signal handler management in threads (#3266)
 * rc1: fix local connector retries (#3301)

### Cleanup

 * remove flux-hwloc reload command and aggregator module (#3296)
 * doc: add flux-jobs(1) examples (#3295)
 * job-manager / job-info: misc cleanup (#3246)
 * build: increase minimum version of jansson to 2.10 (#3240)
 * ci: ensure pylint script fails when lint warnings are produced (#3269)


flux-core version 0.20.0 - 2020-09-30
-------------------------------------

This release features changes to support Flux as the native resource
manager on small (<= 256 node) clusters, for testing only.  A draft system
administration guide is available at:

https://flux-framework.readthedocs.io/en/latest/adminguide.html

### New features

 * hwloc: add printing of num GPUs to `flux hwloc info` (#3217)
 * resource: mark nodes down when they are stopped (#3207)
 * broker:  allow late-joining brokers, execute rc1/rc3 on all ranks (#3168)
 * shell/pmi: add improved PMI key exchange mechanism (#3219)

### Fixes

 * job-manager: communicate job priority changes to job-info (#3208)
 * job-info: handle annotations race (#3196)
 * python/job: Update `state_single` default header (#3227)
 * libidset: reject idset strings that don't conform to RFC 22 (#3237)
 * job-info: handle job-priority changes (#3208)
 * doc: list sphinx as a doc dependency in README.md (#3225)
 * testsuite: fix race in python SIGINT test (#3224)
 * job-manager: fix segfault changing priority of a running job (#3220)
 * shell: allow multiple resources per level in jobspec (#3175)
 * python: allow Ctrl-C interrupt of `Future.get()` and `wait_for()` (#3215)
 * shell: use F58/alternate encodings in output file template {{id}} (#3206)
 * fallback to ASCII for F58 FLUIDs with `FLUX_F58_FORCE_ASCII` (#3204)
 * rc: load sched-simple only if no other scheduler is loaded (#3177)
 * docker: do not install Sphinx via pip in Centos8 image (#3195)
 * flux-jobs / python bindings: handle empty string conversions (#3183)

### Cleanup

 * reduce log noise (#3226)
 * flux-comms: remove obsolete command (#3211)


flux-core version 0.19.0 - 2020-08-31
-------------------------------------

Notable features and improvements in this release include enhanced
support for tools/debuggers (e.g. STAT, LaunchMON and TotalView), a
new set of `--env` environment manipulation options for flux-mini(1),
better support for listing jobs through the Python API, and a fix
for an annoying usability issue with F58 encoded jobids in non-UTF-8
locales.


### New features

 * switch to utf-8 for subprocess and job io encoding (#3086)
 * improve support for shell plugin developers (#3159, #3132)
 * flux-mini: add environment manipulation options (#3150)
 * flux-mini: add --debug option for tools support (#3130)
 * bash: top level command completions for flux (#2755)
 * add fluxorama system instance docker image sources (#3031, #3128)
 * content-s3: add configuration, support for libs3 variants (#3067, #3115)
 * Use F58 JOBIDs in most user-facing commands (#3111)
 * broker: state machine refactoring (#3107)
 * broker: restore client-side PMI logging (#3105)
 * libflux: add `flux_module_set_running()` (#3104)
 * python: Add JobInfo, JobInfoFormat, and JobList classes (#3174)

### Fixes

 * Fix F58 encoding in non-multibyte locales (#3144)
 * job-info,job-shell: allow non-V1 jobspec (#3160)
 * build: fix innocuous configure error (#3129)
 * travis-ci: fix ARGS when `DOCKER_TAG` set (#3125)
 * doc: fix flux-help(1) output and rendering of NODESET.rst (#3119)
 * flux-job: add `totalview_jobid` support and misc. fixes (#3130)
 * small build/test/doc fixes (#3100)
 * fix GitHub project license detection (#3089)
 * shell/lua.d/openmpi: set env vars to force the use of flux plugins (#3099)
 * job-info: do not fail on invalid jobspec / R / eventlog (#3171)
 * flux-module: extend first column of flux-module list output (#3178)

### Cleanup

 * python: split flux.job module into multiple files (#3162)
 * python: reformat with latest black formatter, pin black version (#3169)
 * libflux: fix comment in module.h to reference readthedocs (#3138)
 * Update rfc links to RTD site (#3137)
 * remove the simple dynamic string (sds) code from libutil (#3135)
 * Doc Cleanup (#3117)
 * AUTHORS: remove (#3090)

flux-core version 0.18.0 - 2020-07-29
-------------------------------------

This release features a more compact default representation for Flux JOBIDs,
manual pages converted to ReST format and published on
[readthedocs.io](https://flux-framework.readthedocs.io/projects/flux-core/),
and the ability for schedulers to add data to jobs which can be displayed
with `flux jobs`.

### New features

 * doc: man pages converted to ReST for publication on readthedocs.io
   (#3033, #3078, #3085)
 * Add support for RFC19 F58 encoded JOBIDs (#3045)
 * Support user and scheduler job annotations (#3065, #3062, #2960)
 * add content-s3, content-files alternate backing stores (#3025, #2992)
 * Python interface to 'mini batch' (#3020)

### Fixes

 * shell: fix bug in cpu-affinity=per-task (#3080)
 * flux-hwloc: remove ignore of `HWLOC_OBJ_GROUP` (#3046)
 * cmd: Make label io options consistent (#3068)
 * flux-resource list: Allow null/missing key to designate empty set (#3047)
 * flux-jobs: small functionality and testing updates (#3060)
 * job-manager: avoid segfault on priority change with pending alloc (#3072)

### Cleanup

 * doc: adjust dependency table to reflect hwloc v2.0+ support (#3053)
 * Update terminology to use more inclusive words (#3040)

### Testsuite enhancements

 * testsuite: remove use of -S option in `run_timeout` (#3079)
 * testsuite: minor valgrind test cleanup (#3077)
 * docker: small updates for testenv images, travis builds (#3058)
 * travis-ci: add python coverage (#3056)
 * travis-ci: Add `--localstatedir=/var` to docker tag builds (#3050)
 * pylint: Update pylint to 2.4.4 (#3035)
 * Fix testsuite for Lua 5.3 on Ubuntu 20.04 (#3028)
 * docker: really actually fix Ubuntu 20.04 (focal) docker tags (#3027)
 * travis-ci: enforce correct configure ARGS for docker tags (#3023)
 * travis: tag a docker image for ubuntu 20.04 (#3022)
 * python: add stdio properties to Jobspec class (#3019)
 * build and test fixes (#3016)


flux-core version 0.17.0 - 2020-06-18
-------------------------------------

*NOTE*: Support has been removed for Python 2.

### New features

 * Improved interface for batch jobs: `flux mini batch` and `flux mini alloc`
   (#2962)
 * Pty support for Flux jobs via `-o pty` shell option (#2894)
 * New resource module for monitoring and control of resources,
   including ability to exclude and drain/undrain ranks. (#2918, #2949)
 * New `flux resource` utility to drain and list resources. (#2949)
 * Multiple improvements for `flux jobs`: colorize output, add "status"
   and "exception" fields, allow jobids as positional arguments, and
   add a custom conversion type `h` for "-" (#2798, #2858, #2902, #2910,
   #2940, #2926, #2865)
 * Support for hwloc v2.0+ (#2944)
 * Support for MPIR debugging of jobs (#2654)
 * New job-archive module optionally stores job data in sqlite. (#2880)
 * single-broker system instance support, including minimal
   support for restart (archived job information is saved) (#2783, #2820,
   #2813, #2809)
 * Add support for multi-user execution (#2822, #2813)
 * Add support for enforcing job time limits (#2995)
 * python: Add bindings for job cancel and kill (#2976)
 * python: Add bindings for watching job eventlog events (#2986)

### Improvements

 * support systemctl reload flux (#2879)
 * enhance job throughput (#2777, #2792)
 * sched-simple: schedule cores instead of PUs by default (#2966)
 * broker: send service.disconnect requests on module unload (#2913)
 * broker: add interface for monitoring broker liveness (#2914)
 * broker: add cleanup phase (#2971)
 * broker: only allow userid- services to be registered by guests (#2813)
 * libflux: add `flux_msg_last_json_error(3)` (#2905)
 * flux-job: Use common attrs for list cmds (#2901)
 * doc: add flux job shell API manpages (#2793)
 * job-info: Support "exception" and "success" list attributes (#2831, #2858)
 * job-info: improve error responses from various list RPCs (#3010)
 * rc: load job-info on rank 0 only (#3009)
 * python: remove support for Python 2 (#2805)
 * python: cache python wrappers in the class (#2878)
 * python: tweaks in preparation for flux-tree-helper (#2804)
 * python: add 'flux_job_list_inactive' Python binding (#2790)
 * python: allow reactor_run() to be interrupted (#2974)
 * config: parse TOML once in broker, share with modules (#2866)
 * config: use config file for access policy (#2871)
 * docker: add default PS1 that includes flux instance size, depth (#2925)
 * docker: start munge in published docker images (#2922)

### Fixes

 * Fix compilation under GCC 10.1.0 (#2954)
 * librouter: avoid dropping messages on EPIPE (#2934)
 * README: update documentation link (#2929)
 * README.md: fix required Lua version (#2923)
 * README: add missing dependencies: aspell-en and make (#2889)
 * shell: make registered services secure by default (#2877)
 * cmd/flux-kvs: Fix segfault in dir -R (#2847)
 * job-exec: drop use of broker attrs, use conf file or cmdline instead
   (#2821)
 * broker: clean shutdown on SIGTERM (#2794)
 * flux-ping: fix problems with route string (#2811)
 * libsubprocess:  don't clobber errno in destructors, handle ENOMEM (#2808)
 * Fix flux-job status for jobs with exceptions before start (#2784)
 * shell: Add missing R member to shell info JSON object (#2989)
 * job-ingest: fix validation of V1 jobspec (duration required) (#2994)
 * doc: fixes and updates for idset manpages (#3012)

### Cleanup

 * removed outdated pymod module (#3008)
 * broker and flux-comms cleanup (#2907)
 * cmd/flux-kvs: Remove legacy --json options and json output (#2807)
 * doc: Fix typos in man pages (#2725)
 * libutil: improve out of memory handling, conform to RFC 7 (#2785)
 * content-sqlite, content-cache: cleanup and refactoring (#2786)

### Testsuite enhancements

 * Fix skipped tests in t2205-hwloc-basic.t (#2998)
 * t2204-job-info: Split up tests into new files (#2957)
 * t/t2800-jobs-cmd: Fix racy test (#2951)
 * t: add `HAVE_JQ` prereq to tests that use `jq` (#2936)
 * sharness: fix TEST_CHECK_PREREQS for tests using $jq (#2939)
 * job-info: module & test cleanup (#2932)
 * testsuite: add ability to ensure programs are used under appropriate
   prereqs (#2937)
 * ensure unit tests do not link against installed flux libraries (#2917)
 * t2204-job-info: Fix racy tests (#2862)
 * test rehab: new flexible run_timeout, speeding up asan, and many more
   timeouts and test repairs (#2849)
 * Mypy: add static type checking for python to travis (#2836)
 * testsuite: minor fixes and slight improvements (#2842)
 * README: update Travis CI badge after transition to travis-ci.com (#2843)
 * tests: timeout in automake harness (#2840)
 * t/t0005-exec: Increase timeout lengths (#2828)


flux-core version 0.16.0 - 2020-02-24
-------------------------------------

## New features

 * job-info: fix ordering of pending jobs (#2732)
 * job-info: add list-id service for race-free listing of 1 jobid (#2720)
 * sched-simple: add unlimited alloc mode (#2726)
 * flux-module: add `flux module reload` subcommand (#2736)
 * flux-queue: add `flux queue idle` subcommand (#2712)

## Improvements

 * broker: rework shutdown: rc3 no longer under grace-time timeout (#2733)
 * broker: log dropped responses sent down overlay (#2761)
 * libflux: fulfill empty "wait_all" futures immediately (#2714)
 * libflux: allow anonymous futures in `flux_future_push(3)` (#2714)
 * shell: report meaningful exit codes for ENOENT, EPERM & EACESS (#2756)
 * flux-jobs: refactor using new JobInfo and OutputFormat classes (#2734)
 * python: accept integer job duration (#2702)
 * python: switch from flags to boolean args in job.submit(), submit_async()
   (#2719)
 * python: return derived JobListRPC and JobWaitFuture objects from
   job.job_list and job.wait,wait_async for a better interface (#2753)

## Fixes

 * broker: fix bootstrap under openpmix PMI-1 compat library (#2748)
 * broker: mute modules during unload to avoid deadlock (#2710)
 * libflux: block `flux_send()` during handle destruction (#2713)
 * job-ingest: fixes for validation worker management (#2721, #2716)
 * build: fix compilation errors on clang < 6.0 (#2742)
 * testsuite: fix tests when run under Slurm and Flux jobs (#2766)
 * testsuite: fix for hangs in tests using rc3-job (#2744)
 * doc: fix URI format in flux-proxy(1) manpage (#2747)


flux-core version 0.15.0 - 2020-02-03
-------------------------------------

## Summary:

This release fixes a critical issue (#2676) with `flux module remove` in
flux-core-0.14.0 that causes rc3 to fail when flux-core is integrated
with flux-sched.

### New features

 * flux-job: add raiaseall, cancelall, killall (#2678)
 * flux-queue: new command to control job queue (#2659, #2687)
 * flux-jobs: support listing `nnodes` and `ranks` (#2656, #2705)

### Improvements

 * shell: expand lua api to improve error handling in shell rc scripts (#2699)
 * shell: improve error messages to users on exec failures (#2675)
 * flux-job: (attach) fetch log messages even when shell init fails (#2691)
 * flux-job: (attach) add `-v` option (adds file,line log messages) (#2691)
 * flux-job: (list) make filtering options match `flux jobs` (#2639)
 * flux-job: (list) make JSON output the default (#2636)
 * flux-job: (drain,undrain) drop subcommands (see flux queue) (#2659)
 * job-info: transition state _after_ retrieving data from KVS (#2655)
 * job-info: add checks in sharness test to avoid racyness (#2666)
 * job-info: rename attributes to ease parsing (#2643)
 * flux-jobs: add --from-stdin option and other small fixes (#2648)
 * python: allow JobspecV1 to accept 0 gpus_per_task (#2701)
 * optparse: always display `--help` usage first in command help output (#2691)
 * libflux: add message cred helpers (#2670)
 * github: check flux-sched@master against submitted flux-core PRs (#2680)

### Fixes

 * shell: fix bad exit from mvapich rc script, avoid flux.posix in rcs (#2699)
 * shell: fix race between stdin/out readers and eventlog creation (#2688)
 * shell: install `shell.h`: the public api for shell plugins (#2690)
 * shell: `chdir()` into current working directory (#2682)
 * rc: improve rc3 reliability, add `flux module remove -f` option  (#2676)
 * testsuite: fix unsafe getenv in libpmi tests, /tmp usage in sharness (#2669)
 * job-manager: fix counting problem that leads to scheduler sadness (#2667)


flux-core version 0.14.0 - 2020-01-12
-------------------------------------

## Summary:

This version of flux-core improves the reliability and performance
of the new execution system, and fills gaps in the previous release.
Some highlights are:

 * support for jobs reading standard input
 * improved job listing tool - see flux-jobs(1)
 * improved python support for building jobspec and waiting for job completion
 * ability to override job names displayed in listing output

### New features

 * Add porcelain `flux jobs` command (#2582)
 * job-info: use basename of arg0 for job-name (#2598)
 * job-info: honor `max_entries` option in job-info.list (#2596)
 * job-info: Support task-count in listing service (#2580)
 * Support job state times in job listing service (#2568)
 * python: add jobspec classes to main bindings (#2534)
 * initial job-name support (#2562)
 * job-manager: add `flux_job_wait()` (#2546)
 * shell: add support for debugger synchronization and `MPIR_proctable` gather
   (#2542)
 * job-info: Add stats for number of jobs in each state (#2540)
 * job-info: re-load job state from KVS (#2502)
 * libflux: add `flux_get_conf()` (#2501)
 * job-info: Store full job-history, allow users to query pending, running,
   and inactive jobs (#2471)
 * Initial shell stdin support (#2448)

### Improvements

 * libflux/mrpc: drop the mrpc class (#2612)
 * docker: add image and travis tests on CentOS 8 (#2610)
 * mergify: do not auto-merge PRs with invalid commits (#2603)
 * broker: new format for [bootstrap] configuration (#2578)
 * broker/boot_config: use new config file interfaces (#2524)
 * shell: add unpack-style helpers for get_info shell plugin api calls
   (#2573)
 * testing/asan: enable asan in test framework and travis-ci (#2466)
 * README.md: update build docs for Python 3 (#2565)
 * Update jobspec command key per RFC 14 changes (#2564)
 * replace exec "running" event with "shell.init" and "shell.start" (#2541)
 * shell: improve stdout/stderr performance (#2531)
 * modules/job-manager:  [cleanup] simplify queue listing and refactor
   internal context (#2536)
 * kvs: improve append performance (#2526)
 * shell: generate job exception on `MPI_Abort` (#2510)
 * `msg_handler`: make `topic_glob` `const char *`, fix fallout (#2496)
 * libflux: fall back to builtin connector search path (#2489)
 * README: minor source cleanup (#2509)
 * shell: implement shell-specific log facility, add support for log events
   to output eventlog (#2477)
 * flux-mini: improve handling of `--setattr` and `--setopt` (#2495)
 * bindings/python: reinstate python2 support (#2482)
 * bindings/python: change minimum python version to 3.6 (#2452)
 * libutil: replace fdwalk with version that uses getdents64 (#2479)
 * flux-shell: handle jobspec command as bare string (#2484)
 * librouter: factor common code from connector-local, flux-proxy (#2354)
 * mergify: fix rule that prevents merging of "WIP" PRs (#2453)
 * buffer: start buffer at 4K and grow to 4M as necessary (#2449)
 * libioencode: make rank parameters strings (#2441)
 * flux-kvs: Add eventlog namespace option (#2439)
 * testsuite: fix LONGTEST and other small improvements (#2444)
 * job-ingest: switch to v1 schema (#2433)
 * libtomlc99: update for TOML v0.5.0 support #2619
 * job-ingest: switch to a py bindings based jobspec validator (#2615)

### Fixes

 * flux-job: misc fixes for attach (#2618)
 * fix minor issues found by lgtm scan (#2605)
 * broker: increase nofile limit to avoid assertion failure in `zuuid_new()`
   (#2602)
 * use libuuid instead of zuuid (#2606)
 * github: enable a workflow to validate commits in a PR (#2586)
 * python: fix circular reference in `Future` class (#2570)
 * have future take a ref on `flux_t` handle (#2569)
 * bindings/python and libev: work around future leak (#2563)
 * kvs: Fix duplicate append corner case (#2555)
 * shell: stdin write to exited task should not cause fatal job exception
   (#2550)
 * job-manager: fix internal job hash lookup error handling (#2552)
 * shell: fix segfault if logging function is called in or after
   `shell_finalize()` (#2544)
 * kvs: fix memory use-after-free corner case (#2525)
 * t: fix tests prone to races or timeouts on constrained systems (#2523)
 * job-exec: fix memory errors detected by valgrind (#2521)
 * test: fix random cronodate test failure (#2520)
 * t1004-statwatcher: fix test on Ubuntu 19.10 (#2513)
 * job-ingest: launch `.py` validators with configured python (#2506)
 * doc: `flux_respond_raw` doesn't take an errnum (#2504)
 * Fix infinite recursion when wrapper.Wrapper object initialized with
   incorrect args (#2485)
 * sched-simple: fix `rlist_alloc_nnodes()` algorithm (#2474)
 * fix crash in `is_intree()` with EACCESS or ENOENT from builddir (#2468)
 * testsuite: extend some testing timeouts  (#2451)


flux-core version 0.13.0 - 2019-10-01
-------------------------------------

## Summary:

This version of flux-core enhances the new execution system to near full
functionality, including new tools for job submission, better MPI support,
task and GPU affinity options, and flexible job output handling including
redirection to bypass the KVS. A powerful shell plugin infrastructure allows
execution features to be selectively enabled by users.

See flux-mini(1) for more info on the new job submission interface.

Some deficiencies present in this release:

 * flux job list doesn't show inactive jobs
 * no per-task output redirection
 * output is space-inefficient in KVS (base64 encoding, one commit per line)
 * no stdin redirection
 * need better shell task cleanup and early task exit detection
 * no debugger support (MPIR)

### New features

 * flux-mini: new run/submit interface (#2409, #2390)
 * flux-version: make flux -V,--version an alias, add manpage (#2412, #2426)
 * shell: add gpu affinity support (#2406)
 * shell: add builtin core affinity plugin (#2397)
 * shell: Support stdout/stderr redirect to a file (#2395)
 * shell: add support for plugins and shell initrc (#2376, #2392, #2357, #2335)
 * shell: flush output to KVS on every line (#2366, #2332)
 * shell: limit the number of I/O requests in flight (#2296)
 * shell: use RFC 24 eventlog output (#2308)
 * flux-job attach: add timestamps, --show-exec option (#2388)
 * libioencode: convenience library for encoding io (#2293)
 * libsubprocess: add start/stop for streams (#2271, #2333)
 * libsubprocess: add `flux_subprocess_kill()` (#2297)
 * job-info: development in support of job output (#2341, #2374, #2360,
   #2303, #2307)
 * flux-in-flux: flux --parent option, add `instance-level`, `jobid`
   broker attributes (#2326, #2362)
 * flux-in-flux: set `local_uri`, `remote_uri` in enclosing instance KVS (#2322)

### Improvements

 * libflux/reactor: add `flux_reactor_active_incref()`, `_decref()` (#2387)
 * libflux/module: add `flux_module_debug_test()` (#2373)
 * libschedutil: export library for use by flux-sched and others (#2380)
 * libschedutil: destroy pending futures on scheduler unload (#2226)
 * libflux/message: drop `flux_msg_sendfd()`, `_recvfd()` from API (#2375)
 * libflux/message: add `flux_msg_incref()` and `_decref()` (#2334)
 * libflux: update message dispatch to support routers (#2367)
 * libflux/buffer: increase efficiency of line buffered I/O (#2294)
 * libsubprocess: cleanup ( #2343, #2286)
 * testsuite improvements (#2404, #2400)
 * build system cleanup (#2407)
 * documentation cleanup (#2327)
 * abstract in-tree detection into libutil (#2351)
 * libjob: `flux_job_kvs_namespace()` (#2315)

### Fixes

 * build: bump libflux-core.so version to 2 (#2427)
 * sched-simple: reject requests with unknown resource types (#2425)
 * restore libpmi2 to support MPICH/MVAPICH configured for slurm pmi2 (#2381)
 * broker: avoid accidentally consuming % format characters in initial
   program args (#2285)
 * connector-local: suppress EPIPE write errors (#2316)
 * libidset: fix `idset_last()` at size=32 (#2340, #2342)
 * connectors/loop: do not accidentally close STDIN (#2339)
 * job-exec: fix exception handling of jobs in transition states (#2309)
 * broker: don't read `FLUX_RCX_PATH` to set rc1,rc3 paths (#2431)
 * job-ingest: validator shebang can pick the wrong python (#2435)


flux-core version 0.12.0 - 2019-08-01
-------------------------------------

## Summary:

This version of flux-core replaces the old execution prototype, "wreck",
with a new job submission and execution protocol. The new system does
not yet have support for all the features of the prototype, however it
is capable of running jobs specified in version 1 jobspec format with
an advanced and performant job submission API.

For early adopters:
 
 * To generate jobspec, see `flux jobspec`
 * To submit jobspec, see `flux job submit`
 * Instead of `flux wreckrun` try `flux srun`
 * Instead of `flux wreck ls` try `flux job list`
 * Instead of `flux wreck kill/cancel` try `flux job kill/cancel`
 * Job events are recorded detailed eventlog, see `flux job eventlog <id>`
 * Experience job synchronization with `flux job wait-event`
 * Attach to submitted jobs with `flux job attach`
 * Want info about a job? Try `flux job info`
 * Waiting for all jobs to complete? Try `flux job drain`

### New Features:

 * new job submit api and `flux job submit` command (#1910, #1918)
 * add job exception and cancellation support (#1976)
 * support validation for submitted jobspec (#1913, #1922)
 * add `flux jobspec` jobspec generation script (#1920, #1964)
 * add a simple default node/core fcfs scheduler, sched-simple
   (#2038, #2053, #2203)
 * add `flux job info`, `eventlog`, `wait-event`, `attach`
   (#2071, #2085, #2098, #2112, #2114, #2115, #2137, #2142, #2269, #2084)
 * add `flux job drain` (#2092)
 * add flux-shell, the flux job shell (#2211, #2240, #2246, #2244, #2278)
 * add `flux srun` (#2179, #2227)

### Improvements:
 
 * libsubprocess updates (#2158, #2152, #2167, #2174, #2230, #2254, #2262,
    #2265)
 * job-manager: add exec and scheduler interfaces, add job state machine:
    (#2025, #2031, #2067, #2068, #2077, #2146, #2198, #2231)
 * job-manager: add state transition events (#2109)
 * job-manager: other improvements (#2047, #2062)
 * replace resource-hwloc module (#1968)
 * standardize parsing of duration in most tools (#2095, #2216)
 * add guest support to barrier module (#2215)
 * add broker `rundir` attribute (#2121) 
 * kvs: remove namespace prefix support (#1943)
 * kvs: support namespace symlinks (#1949)
 * kvs: new kvs namespace command (#1985)
 * python: add futures support (#2023)
 * improve signature of `flux_respond` and `flux_respond_error` (#2120)

### Fixes

 * misc build and test system fixes (#1912, #1914, #1915, #1925, #1941,
    #2004, #2014, #2019, #2022, #2028, #2034, #2037, #2058, #2104, #2133,
    #2124, #2177, #2221, #2128, #2229)
 * misc flux-broker fixes (#2172, #2178, #2175, #2181, #2194, #2197, 
 * misc kvs fixes (#1907, #1936, #1945, #1946, #1966, #1949, #1965, #1969,
    #1971, #1977, #2011, #2016, #2018, #2056, #2059, #2064, #2126, #2130,
    #2136, #2138)
 * remove kvs classic library (#2017)
 * misc python fixes (#1934, #1962, #2046, #2218)
 * misc libflux-core fixes (#1939, #1942, #1956, #1982, #2091, #2097, #2099,
    #2153, #2164)
 * do not version libpmi*.so (#1992)
 * ensure system python path is not pushed to front of PYTHONPATH (#2144)
 * flux-exec fixes: (#1997, #2005, #2248)
 * libpmi fixes (#2185)
 * libidset fixes (#1928, #1975, #1978, #2060)
 * jobspec fixes and updates (#1996, #2081, #2096)
 * other fixes (#1989, #2090, #2151, #2280, #2282)

 
flux-core version 0.11.0 - 2019-01-03
-------------------------------------

### Fixes
 * flux-module: increase width of size field in list output (#1883)
 * kvs: return errors to callers on asynchronous load/store failures (#1836)
 * flux-start: dispatch orphan brokers, fully clean up temp directories (#1835)
 * flux-exec: ensure stdin is restored to blocking mode on exit (#1814)
 * broker: don't connect to enclosing instance (#1798)
 * flux (command): handle inaccessible build directory, fix PATH issue (#1683)
 * wreck: fix incorrect error handling in job module (#1617)
 * libflux: improve efficiency of asynchronous futures (#1840)
 * libflux: fix composite future implementation (#1791)
 * libflux: improve lookup efficiency of RPC message handlers (#1807)
 * libflux: give all aux set/get interfaces uniform semantics (#1797)
 * update to libev 4.25, ensure valgrind runs clean on i686 (#1898)

### New Features
 * license: re-publish project under LGPLv3 (#1829, #1788, #1901)
 * wreck: use direct stdio transport, unless -okz option (#1875, #1896, #1900)
 * wreck: add new -J, --name=JOBNAME option to flux-wreckrun and submit (#1893)
 * libflux: support queue of future fulfillments (#1610)
 * libflux: support dynamic service registration (#1753, #1856)
 * kvs: replace inefficient KVS watch implementation and outdated API (#1891,
   #1890, #1882, #1878, #1879, #1873, #1870, #1868, #1863,
   #1861, #1859, #1850, #1848, #1820, #1643, #1622)
 * job: add job-ingest, job-manager modules, and API (experimental)
   (#1867, #1774, #1734, #1626)
 * libidset: expand API to replace internal nodeset class (#1862)
 * libflux: add KVS copy and move composite functions (#1828)
 * libflux: access broker, library, command versions (#1817)
 * kvs: restart with existing content sqlite, root reference (#1800, #1812)
 * python: add job & mrpc bindings (#1757, #1892)
 * python: add flux python command to run configured python (#1766)
 * python: add flux-security bindings (#1716)
 * python: Python3 compatibility (#1673)
 * kvs: add RFC 18 eventlog support (#1671)
 * libsubprocess: cleanup and redesign
   (#1713, #1664, #1659, #1658, #1654, #1645, #1636, #1629)
 * libflux/buffer: Add trimmed peek/read line variants (#1639)
 * build: add library versioning support (#1874)
 * build: add support for asciidoctor as manpage generator (#1650, #1676)
 * travis-ci: run tests under docker (#1688, #1684, #1670)

### Cleanup
 * libflux: drop broker zeromq security functions from public API (#1846)
 * libflux: clean up interface for broker attributes (#1845)
 * libflux: drop reduction code from public API (#1844)
 * libutil: switch from munge to libsodium base64 implementation (#1786)
 * python: python binding is no longer optional (#1772)
 * python: add "black" format check, and reformat existing code (#1802)
 * python/lua: avoid deprecated kvs functions (#1748)
 * kvs: misc cleanup, refactoring, and fixes
   (#1805, #1813, #1773, #1764, #1712, #1696, #1694)
 * broker: drop epgm event distribution (and munge dependency) (#1746)
 * content-sqlite: switch from lzo to lz4 (#1740)
 * libpmi: drop PMIx client support (#1663)
 * libpmi: avoid synchronous RPCs in simple-server kvs (#1615)
 * modules/cron: misc cleanup (#1657)
 * RFC 7: fix various style violations (#1705, #1717, #1706, #1611)
 * gcc8: fix output truncation (#1642)
 * sanitizer: fix memory leaks (#1737, #1736, #1739, #1737, #1735, #1733)
 * build: misc. cleanup and fixes (#1886, #1795, #1824, #1827, #1701, #1678)
 * test: misc. cleanup and fixes (#1644, #1704, #1691, 1640)


flux-core version 0.10.0 - 2018-07-26
-------------------------------------

### Fixes
 * fix python kz binding errors (#1537)
 * fix default socket path and config file parsing for flux-broker (#1577)
 * Lua 5.2 compatibility and other Lua fixes (#1586, #1594)
 * flux PMI server response before closing (#1528)

### New Features
 * support cpu affinity for wreck jobs (#1533, #1603)
 * support for GPU device discovery through hwloc (#1561)
 * set CUDA_VISIBLE_DEVICES for jobs with GPUs (#1599)
 * add ability to bootstrap Flux using pmix (#1580)
 * add `flux wreck sched-params` cmd to tune scheduler at runtime (#1579)
 * support `-o mpi=spectrum` for spectrum MPI launch (#1578, #1588)
 * allow generic JSON values in aggregator (#1535)
 * new --wrap=arg0,arg1 option to flux-start (#1542)
 * allow arbitrary error strings in RPC responses (#1538)
 * support for composite flux_future_t types (#1553)
 * add buffered I/O support to Flux API (#1518, #1547, #1548)
 * remove extra line breaks in Flux log messages (#1530)
 * add Flux Locally Unique ID (FLUID) implementation (#1541)

### Cleanup
 * remove json-c (#1522, #1524, #1525, #1527, #1529)
 * libidset internal cleanup (#1521)
 * libsubprocess cleanup (#1549)
 * drop PMIx heuristic in libpmi (#1575)
 * add missing `#!/bin/bash` to all rc1 scripts (#1597)

flux-core version 0.9.0 - 2018-05-10
------------------------------------

### Fixes
 * numerous memory leak fixes (#1494, #1322)
 * better support for C++ code (#1225, #1223, #1204)
 * massive scalability improvement for libkz readers (#1411, #1424)
 * increase job submission throughput (#1472, #1389)
 * reduce amount of information collected in resource-hwloc to
    enhance large instance startup (#1457)
 * i686 portability fixes (#1296)
 * fixes for `flux-kvs dir` and `ls` usage (#1444, #1452)
 * fix for clock_gettime workaround in Lua bindings (#1371)
 * update minimum libhwloc to 1.11.1 to avoid assertion failure (#1478)
 * fix incorrect output from option parsing when invalid short
    option is grouped with valid options in many commands (#1183)
 * fix thread cancellation in sqlite module (#1196)
 * fix segfault on 32bit systems in cron module (#1178)
 * log errors from event redistribution (#1457)
 * increase number of open files in `wrexecd` (#1450)
 * fix job hangs during final task IO output flush (#1450)
 * fixes for `flux-wreck purge` (#1357)
 * scalability fixes for `flux-wreck` subcommands (#1372)
 * general reduction in log messages at INFO level (#1450)
 * improve valgrind.h detection (#1502)
 * fix pkg-config pc name for liboptparse (#1506)
 * fix flux executable run-from-build-tree auto-detection (#1515)

### New Features
 * support config file boot method for broker (#1320)
 * new `flux-kvs ls` command (#1172, #1444)
 * new kvs transaction API (#1346, #1348, #1351)
 * support for KVS namespaces (#1286, #1299, #1316, #1323, #1320, #1327,
    #1336, #1390, #1423, #1432, #1436)
 * support for node inclusion,exclusion via flux-wreck command (#1418)
 * initial parser for jobspec (#1201, #1293, #1306)
 * store child instance URI in enclosing instance (#1429)
 * new `flux-wreck uri` command to fetch child instance URIs (#1429)
 * additional states from kvs module (#1310)
 * append support for KVS values (#1265)
 * support multiple blobrefs per valref in kvs (#1227, #1237)
 * add `flux_kvs_lookup_get_raw`(3) (#1218)
 * add `flux_kvs_lookup_get_key`(3) (#1414)
 * add `flux_event_publish`(3) to libflux API (#1512)
 * support for composite futures in libflux (#1188)
 * add `flux_future_reset`(3) to support multi-response RPCs (#1503)
 * new libflux-idset library (#1498)
 * support raw payloads in `flux-event` (#1488)
 * add raw encode/decode to `flux_event_*` API (#1486)
 * introduce `R_lite` format for job allocation description (#1399, #1485)
 * new `flux-hostlist` command for listing hostnames for jobs (#1499)
 * new `flux-wreck` environment manipulation commands (#1405)
 * `flux-wreck ls` returns active jobs first (#1481)
 * `flux-wreck` tools allow filtering on active,inactive jobs (#1481)
 * `flux-wreckrun` will now block until job is scheduled by default, use the
    new --immediate flag to get old behavior (#1399)
 * add `flux-wreck cancel` command to cancel pending job (#1365, #1367, #1385)
 * add `flux-wreck dumplog` command to dump error log for jobs (#1450)
 * add new `KZ_FLAGS_NOFOLLOW` flag to avoid blocking when no data in a kz
    file (#1450)
 * add `-n, --no-follow` option to `flux-wreck attach` (#1450)
 * propagate gpu and cores information for `flux-wreckrun` and `submit`
   (#1399, #1480)
 * use cmb.exec service to launch `wrexecd`, not direct exec (#1508)
 * new `completing` state for jobs (#1513)
 * support job epilog pre-complete and post-complete scripts (#1513)
 * support output to stderr with `flux_log` functions (#1192)
 

### Cleanup
 * kvs: major cleanup (#1154, #1177, #1182, #1190, #1214, #1213, #1233,
    #1235, #1242, #1243, #1244, #1246, #1248, #1253, #1257, #1262, #1263,
    #1268, #1269, #1273, #1274, #1276, #1279, #1300, #1301, #1304, #1308,
    #1309, #1301, #1314, #1315, #1321, #1329, #1339, #1342, #1343, #1347,
    #1349, #1353, #1383, #1402, #1404, #1440, #1458, #1466, #1477)
 * kvs: improved test coverage (#1291)
 * Add const to message payload accessor functions (#1212)
 * rename `flux_mrpcf`, `flux_mrpc_getf` to `flux_mrpc_pack`,`unpack` (#1338)
 * cleanup bulk message hanglers in libflux (#1277)
 * minor `flux_msg_handler` cleanup (#1171)
 * broker: cleanup to prepare for dynamic service registration (#1189)
 * broker: general cleanup (#1230, #1234, #1241)
 * Change key lwj to jobid in all jsc/wreck messages (#1409)
 * libjsc cleanup (#1374, #1395, #1509)
 * testsuite updates (#1167, #1175, #1313, #1464, #1266)
 * Internal libutil and libflux cleanup (#1319, #1283, #1229, #1231, #1166)
 * build system cleanup (#1163, #1354, #1184, #1200, #1275, #1252)
 * disable pylint by default (#1255, #1258)
 * partial migration from json-c to jansson (#1501, #1508) 
 * drop unused `ev_zlist` composite watcher (#1493)


flux-core version 0.8.0 - 2017-08-23
------------------------------------

#### Fixes
 * libflux: remove calls to functions that exit on error (#1060)
 * fix flux_reactor_run() to return active watcher count (#1085)
 * fix flux path detection when install path contains symlinks (#1122)
 * lua: fix refcount bug in kvs bindings (#1116)
 * kvs: oom() fixes (#1124, #1128)
 * kvs: Fix forced dirty bit clear error (#1133)
 * kvs: fix invalid memory read (#1065)
 * kvs: directory walk return error fixes (#1058)
 * kvs_classic: fix kvs(dir)_put_double (#1114)
 * fix memory leaks detected by valgrind (#1076)
 * avoid deadlock when unloading connector-local module (#1027)
 * fix several arm7l portability issues (#1023)
 * optparse: test and allow adjustment of posixly-correct behavior (#1049)
 * Small improvements for systemd unit file and install paths (#1037)
 * fix small leak in flux cmd driver (#1067)

#### New Features
 * add FLUX_MSGFLAG_PRIVATE and allow guests to content load/store (#1032)
 * allow guests to access hwloc topology (#1043)
 * libflux: new flux_future_t API (#1083)
 * libflux: implement RPCs in terms of futures (#1089)
 * kvs: implement transaction objects (#1107)
 * connector-local: Fix compiler warning (#1031)
 * add optional initial program timeout, for test scripts (#1129)
 * libutil: new dirwalk interface (#1072, #1061, #1059)
 * connector-local: add exponential backoff to connect retry count (#1148)
 * support tbon.endpoint and mcast.endpoint attributes (#1030)
 * content: allow hash type to be configured (#1051)

#### Cleanup
 * update many broker attribute names (#1042)
 * consolidate installed libraries and source tree cleanup (#1095)
 * convert broker from json-c to jansson (#1050)
 * libflux: rename jansson pack/unpack-based Flux API functions (#1104)
 * kvs: various code cleanup (#1057, #1073, #1079, #1099, #1119, #1123, #1153)
 * kvs: refactor kvs commit, lookup, and walk logic (#1066, #1105)
 * kvs: drop unused, legacy and deprecated functions (#1100, #1116)
 * kvs: switch from json-c to jansson (#1108, #1153)
 * Misc Cleanup/Minor Fixes from KVS TreeObject Work (#1152)
 * cron: avoid use of json-c and xzmalloc (#1143)
 * Change void * to void ** in flux_msg_get_payload (#1144)
 * python: make bindings compatible with newer versions of pylint (#1113)
 * barrier: cleanup (#1092)
 * tweak watcher structure, add external watcher construction interface
   (#1082)
 * drop coprocess programming model (#1081)
 * split flux_mrpc() out to its own class (#1080)
 * deprecate some libutil classes (#1047)
 * cleanup of flux_msg_copy(), flux_rpc_aux_set() etc. (#1056)

#### Testing
 * update sharness version to upstream 1.0.0 version (#1035)
 * cleanup kvs tests (#1149)
 * mitigate slow builds in Travis-CI (#1142)
 * fix --chain-lint option in sharness tests (#1125)
 * t2000-wreck.t: fix intermittent failures (#1102, #1109)
 * kvs: Add json_util unit tests (#1106)
 * run valgrind if available as part of make check (#1076, #1098)
 * add FLUX_PMI_SINGLETON env variable to avoid SLURMs libpmi in valgrind
   test (#1091)
 * other test improvements (#1087)
 * update soak test for recent flux changes (#1072)
 * test/security: Fix test corner case (#1029)

#### Documentation
 * add missing manpages, minor manpage fixes (#1045)
 * improve reactor documentation (#1086)
 * Code comments and documentation cleanup (#1138)

flux-core version 0.7.0 - 2017-04-01
------------------------------------

#### Fixes

 * Improve reliability of module unloading (#1017)
 * Update autotools for `make dist` to support newer arches (#1016)
 * Fix corner cases in resource-hwloc module (#1012)
 * Ensure destructors are called during broker shutdown (#1005)
 * `flux-logger(1)` and `flux_log(3)` can return error (#1000)
 * Fix balancing of Caliper hooks in RPC calls (#991)
 * Fix missed errors in subscribe/unsubscribe on local connector (#994)
 * sanitize log entries before they enter circular buffer (#959)
 * Do not send wreck.state.complete event before job archival (#955) 
 * Update embedded libev to 4.24 (#944)
 * Propagate argument quoting properly in `flux-start` and `flux-broker` (#931)
 * Fixes and improvements in liboptparse (#922, #927, #929)
 * Tighten up PMI implementation for OpenMPI (#926)

#### New Features

 * Allow user other than instance owner to connect to an instance (#980)
 * Systemd support, default run directory and URI for system instance
   (#992, #995)
 * New `--bootstrap` option to `flux-start` (#990)
 * New `KVS_NO_MERGE` flag in kvs commit and fence operations (#982)
 * Add `broker.pid` to broker attributes (#954)
 * `flux start` only execs broker if `--size` is not specified (#951)
 * Add pkg-config package for Flux PMI (#921)

#### Cleanup

 * Remove live module (#1003)
 * Remove flux-up and flux-topo (#960)
 * Transition away from deprecated czmq classes (#1013)
 * Re-architect and improve many internal and cmd rpc functions (#1002, #1009)
 * Other major and minor cleanup (#919, #928, #941, #940, #942, #954, #969,
    #976, #981, #978, #986, #990, #1001, #1008)
 * Remove `cmb.` prefix from broker services (#947)

#### Testing

 * Expand and improve unit and system tests for greater code coverage
   (#937, #942, #979, #985, #991, #1004, #1011, #1013, #1014)
 * Fix documentation spellcheck (#1015)
 * Add dependency on "all" to top-level `make check` (#970)
 * Add flake8/pylint checks (#816)

#### Documentation

 * Improve flux_reactor_create documentation (#970)
 * Update flux_msg_cmp(3) and flux_recv(3) to match flux_match changes (#946)
 * Update flux-submit(1) and flux-wreckrun(1) manpages (#945)


flux-core version 0.6.0 - 2016-11-29
------------------------------------

#### Fixes

 * Fixes for ATS testsuite compatibility (#914)
 * python: install kz bindings file (#895)
 * broker: adjust errno response to "upstream" request on rank 0 (#913)
 * Fix for possible unconstrained memory growth in modules/libjsc (#891)
 * Fix error message on flux-help failure (#887)
 * Issue fatal error in wrexecd for invalid tasks on node (#901)
 * Fix barrier protocol incompatability with older jansson versions (#889)

#### New Features

 * Add a flux content service API (#903)
 * Enhance option parsing library for thread safety and new features
  (#908, #910, #911)
 * Add flux_rpcf_multi() (#909)
 * Add new "any" and "upstream" nodeset options (#909)
 * Add HostName key in resource-hwloc `by_rank` directory to allow
   easy resolution of rank to hostname in a flux session (#892)
 * Add `-d` option to `flux-kvs dir`, `dirat`, and `watchdir` to restrict
   output to key only. (#896)

#### Cleanup

 * `flux-ping` refactor and cleanup (#898, #904)
 * Check expected size of `json_int_t` during configure (#902)
 * Other various cleanup, refactoring and testing updates.


flux-core version 0.5.0 - 2016-10-27
------------------------------------

* barrier module cleanup and debug logging (#885)
* Various minor cleanup and documentation updates (#886)
* use jansson >= 2.6 and document JSON payload functions (#884)
* fix MANPATH for Ubuntu, and tidy up travis dep builder (#877)
* fixes for minor issues detected by Coverity (#876)
* build: add --disable-docs configure option (#871)
* kvs: allow get_double to be called on an int (#872)
* README.md: Update srun instructions (#867)
* misc minor fixes (#862)
* make flux_msg_t a bonafide type, add jansson payload accessors (#857)
* Fix --rank issues, add NODESET documentation, and minor cleanup (#860)
* Fix output errors with flux up --comma & --newline, add appropriate tests (#858)
* Add hierarchical lwj directory support in kvs (#811)
* doc/man1/flux-start.adoc: Fix example option usage (#852)
* add dlopen RTLD_DEEPBIND flag (#849)
* src/broker/broker.c: Fix typo flux_repond -> flux_respond (#851)
* doc/man1/flux-module.adoc: Fix environment variable error (#850)
* Pull in json-c, allowing internals to link against alternate json libraries. (#835)
* Add enhanced flux_rpc functions using libjansson json_pack/unpack functions
* Update flux_t * references in man pages (#844)
* Remove pointer from typedef flux_t (#841)
* Remove JSON typedef, just use json_object * (#832)
* module: Remove pointer from typedef flux_modlist_t (#831)
* security: Remove pointer from typedef flux_sec_t (#830)
* and related functions (#824)
* experimental aggregator module (#787)
* kvs: testing, fix use-after-free, streamline python bindings (#823)
* Fix #821: crash in kvs due to NULL arg in Jget_str() (#822)
* python: add a check for invalid handle types (#819)
* Python json and constant rework (#799)
* Python destructor refactoring and exception safety (#807)
* libutil/veb: quiet uninitialized variable warning in vebnew (#809)
* when tagpool is exhausted, grow up to RFC 6 limits (#806)
* add KVS blobref access functions (#801)
* Fix missing error checks in Lua bindings, flux-wreckrun, flux-wreck (#804)
* python: Several fixes for the bindings (#794)
* Switch lua scripts to use lua interpreter in PATH (#789)
* restructure kvs commit handling code for correctness (#788)
* broker/hello: fix leak/error detection in flux_rpc (#786)
* implement scalable reduction for wireup protocol (#781)
* wreck: minor enhancements for scale testing (#782)
* increase KVS commit window (#780)
* autogen.sh: run libtoolize before autoreconf (#771)
* clean up LOG_INFO output, log wireup, rc1, rc3 times, add pmi timing. (#769)
* optparse: remove requirement for option key on long-only options (and other fixes) (#768)

#### Testing

* add test to verify KVS int can be read as double (#878)
* travis-ci: minor updates (#865)
* jsc test: Add timed waits to avoid races (#859)
* t/t0005-exec.t: Fix corner case in test for file not found (#848)
* Fix make distcheck (#847)
* t/t2000-wreck.t: Fix invalid compare on per-task affinity test (#837)
* t/t2000-wreck.t: Fix invalid compare on 'wreckrun: --input=0 works' test (#836)
* travis.yml:  Fix ANCHOR definition (#767)

flux-core version 0.4.1 - 2016-08-12
------------------------------------

* python `kvs_watch()` fix (#759)

* include man7 in distribution (#762)


flux-core version 0.4.0 - 2016-08-11
------------------------------------

#### Scalability improvements

* don't store broken-down hwloc topology in the KVS (#716)

* route rank-addressed requests via TBON (#689)

* streamline matchtag handling (#687)

* keep active jobs in a separate KVS namespace from "archived" jobs (#609)

#### New features

* implement PMI-1 simple server in wrexecd (#706)

* add skeletal PMI-2 library (based on PMI-1) (#747)

* make libflux-optparse.so available externally (#702)

* asynchronous KVS fence and rewritten fence path in KVS module (#707, #729)

* `flux-cron`, a cron/at-like service (#626)

* `flux-proxy` and `ssh://` connector (#645)

#### Other changes

* Use RFC 5424 log format for internal logging, not ad hoc JSON (#691)

* Add wreck lua.d MPI personalities (#669, #743, #747)

* Improved command line for launching flux from slurm/flux (#658)

* Assorted code cleanup.

* Automatic github release upload on tags (#744)

#### Deprecations

* Sophia content backing store module (#727)

* mrpc KVS based muti-RPC interface (#689)

* ZPL config file (#674)

* Ring overlay network (#689)

#### Testing

* Print backtraces for any core files generated in travis-ci (#703)

* Add cppcheck target to travis-ci (#701)

* configure --enable-sanitizer for AddressSanitizer, ThreadSanitizer (#694)

* caliper based profiling (#741)

* coverage uploaded to CodeCof (#751)

* improved test coverage


flux-core version 0.3.0 - 2016-04-26
------------------------------------

* Add support for launching Intel MPI, OpenMPI using PMIv1.
  Use the broker circular log buffer for PMI tracing.

* Add flux wreck timing subcommand which prints time from
  - STARTING: reserved->starting
  - RUNNING:  starting->running
  - COMPLETE: running->complete
  - TOTAL:    starting->complete

* Add three "run levels" for Flux jobs:
  1. run rc1 script on rank 0 to load modules, etc.
  2. run the user's initial program
  3. run rc3 script on rank 0 to unload modules, etc.

* Add module status reporting via keepalive messages.
  `flux module list` now reports live module status:
  - I = intializing
  - S = sleeping
  - X = exited
  - R = running
  - F = finalizing

* Conform to RFC 3 change that requires all JSON payloads to use
  objects as the outermost JSON type (no bare arrays for example).

* Add `flux nodeset` utility so scripts can manipulate nodesets.

* Make `flux env` output suitable for use in bash/zsh eval.

* Drop `flux module --direct` option.  Module load/unload/list is
  now always direct between flux-module and broker(s).
  Drop the `modctl` module for distributed module control.

* When a module fails before entering its reactor loop, propagate
  the error back to `flux module load` so the user knows the
  load was not successful.

* Address memory leaks and adjust KVS usage to ameliorate increasing
  broker memory footprint and degrading job throughput when running
  many small jobs back to back.  Active jobs are now stored under
  `lwj-active` to avoid creating excessive versions of the larger `lwj`
  directory as job state is accumulated.

* Bug fixes to `live` (TBON self-healing) module.  The module is no
  longer loaded by default, pending additional work.  `flux up` will
  always report all ranks up when `live` is not loaded.

* Send keepalives on the ring network and log idle peers on TBON
  and ring at `LOG_CRIT` level, where "idle" means no messages in >= 3
  heartbeats.

* Compress large `content-sqlite` blobs with lzo to reduce disk
  utilization.

* KVS improvements:
  - `kvs_put()` follows intermediate symlinks
  - KVS operations bundled within one commit are applied in order
  - add `kvs_copy()` and `kvs_move()` utility functions.

* Configuration is loaded into broker attribute `config` namespace
  rather than KVS, and is no longer inherited from the enclosing instance.

* `flux` command driver usability improvements.

* Flux API improvements including dropping deprecated functions
  and fine tuning some function signatures (users should recompile).

* Build system allows `--with-tcmalloc`, `--with-jemalloc`, and tcmalloc
  heap profiling.

* Fine tuning of log levels and messages.

* Documentation improvements.

* Test suite improvements/fixes.


flux-core version 0.2.0 - 2016-02-16
------------------------------------

* Avoid putting the Flux libpmi.so in the system ld.so path on systems
  where Flux is installed to the default system prefix, as this could
  interfere with MPI runtimes under other resource managers.

* Enable the SQLite backing store for the KVS by default, which
  addresses unchecked memory growth in the rank 0 broker.

* Stability and usability improvements to the flux-hwloc subcommand,
  and resource-hwloc comms module.

* Added the flux-version subcommand.

* Build system fixes.

* Test suite fixes.

flux-core version 0.1.0 - 2016-01-27
------------------------------------

Initial release for build testing only.

