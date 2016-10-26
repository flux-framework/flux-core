flux-core version 0.5.0 - 2016-10-27
------------------------------------

* [#885](https://github.com/flux-framework/flux-core/pull/885) barrier module cleanup and debug logging
* [#886](https://github.com/flux-framework/flux-core/pull/886) Various minor cleanup and documentation updates
* [#884](https://github.com/flux-framework/flux-core/pull/884) use jansson >= 2.6 and document JSON payload functions
* [#877](https://github.com/flux-framework/flux-core/pull/877) fix MANPATH for Ubuntu, and tidy up travis dep builder
* [#876](https://github.com/flux-framework/flux-core/pull/876) fixes for minor issues detected by Coverity
* [#871](https://github.com/flux-framework/flux-core/pull/871) build: add --disable-docs configure option
* [#872](https://github.com/flux-framework/flux-core/pull/872) kvs: allow get_double to be called on an int
* [#867](https://github.com/flux-framework/flux-core/pull/867) README.md: Update srun instructions
* [#862](https://github.com/flux-framework/flux-core/pull/862) misc minor fixes
* [#857](https://github.com/flux-framework/flux-core/pull/857) make flux_msg_t a bonafide type, add jansson payload accessors
* [#860](https://github.com/flux-framework/flux-core/pull/860) Fix --rank issues, add NODESET documentation, and minor cleanup
* [#858](https://github.com/flux-framework/flux-core/pull/858) Fix output errors with flux up --comma & --newline, add appropriate tests
* [#811](https://github.com/flux-framework/flux-core/pull/811) Add hierarchical lwj directory support in kvs
* [#852](https://github.com/flux-framework/flux-core/pull/852) doc/man1/flux-start.adoc: Fix example option usage
* [#849](https://github.com/flux-framework/flux-core/pull/849) add dlopen RTLD_DEEPBIND flag
* [#851](https://github.com/flux-framework/flux-core/pull/851) src/broker/broker.c: Fix typo flux_repond -> flux_respond
* [#850](https://github.com/flux-framework/flux-core/pull/850) doc/man1/flux-module.adoc: Fix environment variable error
* [#835](https://github.com/flux-framework/flux-core/pull/835) Pull in json-c, allowing internals to link against alternate json libraries.
Add enhanced flux_rpc functions using libjansson json_pack/unpack functions
* [#844](https://github.com/flux-framework/flux-core/pull/844) Update flux_t * references in man pages
* [#841](https://github.com/flux-framework/flux-core/pull/841) Remove pointer from typedef flux_t
* [#832](https://github.com/flux-framework/flux-core/pull/832) Remove JSON typedef, just use json_object *
* [#831](https://github.com/flux-framework/flux-core/pull/831) module: Remove pointer from typedef flux_modlist_t
* [#830](https://github.com/flux-framework/flux-core/pull/830) security: Remove pointer from typedef flux_sec_t
* [#824](https://github.com/flux-framework/flux-core/pull/824) kvs: add kvs_getat() and related functions
* [#787](https://github.com/flux-framework/flux-core/pull/787) experimental aggregator module
* [#823](https://github.com/flux-framework/flux-core/pull/823) kvs: testing, fix use-after-free, streamline python bindings
* [#822](https://github.com/flux-framework/flux-core/pull/822) Fix #821: crash in kvs due to NULL arg in Jget_str()
* [#819](https://github.com/flux-framework/flux-core/pull/819) python: add a check for invalid handle types
* [#799](https://github.com/flux-framework/flux-core/pull/799) Python json and constant rework
* [#807](https://github.com/flux-framework/flux-core/pull/807) Python destructor refactoring and exception safety
* [#809](https://github.com/flux-framework/flux-core/pull/809) libutil/veb: quiet uninitialized variable warning in vebnew
* [#806](https://github.com/flux-framework/flux-core/pull/806) when tagpool is exhausted, grow up to RFC 6 limits
* [#801](https://github.com/flux-framework/flux-core/pull/801) add KVS blobref access functions
* [#804](https://github.com/flux-framework/flux-core/pull/804) Fix missing error checks in Lua bindings, flux-wreckrun, flux-wreck
* [#794](https://github.com/flux-framework/flux-core/pull/794) python: Several fixes for the bindings
* [#789](https://github.com/flux-framework/flux-core/pull/789) Switch lua scripts to use lua interpreter in PATH
* [#788](https://github.com/flux-framework/flux-core/pull/788) restructure kvs commit handling code for correctness
* [#786](https://github.com/flux-framework/flux-core/pull/786) broker/hello: fix leak/error detection in flux_rpc
* [#781](https://github.com/flux-framework/flux-core/pull/781) implement scalable reduction for wireup protocol
* [#782](https://github.com/flux-framework/flux-core/pull/782) wreck: minor enhancements for scale testing
* [#780](https://github.com/flux-framework/flux-core/pull/780) increase KVS commit window
* [#771](https://github.com/flux-framework/flux-core/pull/771) autogen.sh: run libtoolize before autoreconf
* [#769](https://github.com/flux-framework/flux-core/pull/769) clean up LOG_INFO output, log wireup, rc1, rc3 times, add pmi timing.
* [#768](https://github.com/flux-framework/flux-core/pull/768) optparse: remove requirement for option key on long-only options (and other fixes)

#### Testing

* [#878](https://github.com/flux-framework/flux-core/pull/878) add test to verify KVS int can be read as double
* [#865](https://github.com/flux-framework/flux-core/pull/865) travis-ci: minor updates
* [#859](https://github.com/flux-framework/flux-core/pull/859) jsc test: Add timed waits to avoid races
* [#848](https://github.com/flux-framework/flux-core/pull/848) t/t0005-exec.t: Fix corner case in test for file not found
* [#847](https://github.com/flux-framework/flux-core/pull/847) Fix make distcheck
* [#837](https://github.com/flux-framework/flux-core/pull/837) t/t2000-wreck.t: Fix invalid compare on per-task affinity test
* [#836](https://github.com/flux-framework/flux-core/pull/836) t/t2000-wreck.t: Fix invalid compare on 'wreckrun: --input=0 works' test
* [#767](https://github.com/flux-framework/flux-core/pull/767) travis.yml:  Fix ANCHOR definition

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

