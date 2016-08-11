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
    STARTING: reserved->starting
    RUNNING:  starting->running
    COMPLETE: running->complete
    TOTAL:    starting->complete

* Add three "run levels" for Flux jobs:
  1: run rc1 script on rank 0 to load modules, etc.
  2: run the user's initial program
  3: run rc3 script on rank 0 to unload modules, etc.

* Add module status reporting via keepalive messages.
  "flux module list" now reports live module status:
    I = intializing    S = sleeping       X = exited
    R = running        F = finalizing

* Conform to RFC 3 change that requires all JSON payloads to use
  objects as the outermost JSON type (no bare arrays for example).

* Add flux nodeset utility so scripts can manipulate nodesets.

* Make flux env output suitable for use in bash/zsh eval.

* Drop flux module --direct option.  Module load/unload/list is
  now always direct between flux-module and broker(s).
  Drop the "modctl" module for distributed module control.

* When a module fails before entering its reactor loop, propagate
  the error back to "flux module load" so the user knows the
  load was not successful.

* Address memory leaks and adjust KVS usage to ameliorate increasing
  broker memory footprint and degrading job throughput when running
  many small jobs back to back.  Active jobs are now stored under
  "lwj-active" to avoid creating excessive versions of the larger lwj
  directory as job state is accumulated.

* Bug fixes to "live" (TBON self-healing) module.  The module is no
  longer loaded by default, pending additional work.  flux up will
  always report all ranks up when live is not loaded.

* Send keepalives on the ring network and log idle peers on TBON
  and ring at LOG_CRIT level, where "idle" means no messages in >= 3
  heartbeats.

* Compress large content-sqlite blobs with lzo to reduce disk
  utilization.

* KVS improvements:
  - kvs_put() follows intermediate symlinks
  - KVS operations bundled within one commit are applied in order
  - add kvs_copy() and kvs_move() utility functions.

* Configuration is loaded into broker attribute "config" namespace
  rather than KVS, and is no longer inherited from the enclosing instance.

* Flux command driver usability improvements.

* Flux API improvements including dropping deprecated functions
  and fine tuning some function signatures (users should recompile).

* Build system allows --with-tcmalloc, --with-jemalloc, and tcmalloc
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

