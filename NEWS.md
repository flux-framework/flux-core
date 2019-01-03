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

