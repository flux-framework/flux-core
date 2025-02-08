AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(JANSSON_CFLAGS)

#  Always set LUA_PATH such that builddir/?.lua is first so that the
#   build version of fluxometer.lua is found.
#
#  If we're testing with "installed" Flux, then also add path to Lua bindings
#   to LUA_CPATH, so that non-installed test modules can be found (at this
#   time, only lalarm.so)
#
AM_TESTS_ENVIRONMENT = \
	export LUA_PATH="$(builddir)/?.lua;$$LUA_PATH;;;";\
	test -n "$$FLUX_TEST_INSTALLED_PATH" && \
	export LUA_CPATH="$(abs_top_builddir)/src/bindings/lua/.libs/?.so;$$LUA_CPATH;;"; \
	export PYTHONPATH="$(abs_top_builddir)/src/bindings/python:$(abs_top_srcdir)/src/bindings/python:$(top_srcdir)/t/python/tap:$(PYTHON_SITE_PKG):$$PYTHONPATH";\
	export PYTHON="${PYTHON}";

TEST_EXTENSIONS = .t .py
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh
PY_LOG_DRIVER = $(PYTHON) $(top_srcdir)/config/tap-driver.py

lua_SCRIPTS = \
	fluxometer.lua

install-data-local:
	$(INSTALL) -m644 fluxometer/conf.lua.installed \
		$(DESTDIR)$(luadir)/fluxometer/conf.lua
uninstall-local:
	$(RM) $(DESTDIR)$(luadir)/fluxometer/conf.lua

clean-local:
	rm -fr trash-directory.* test-results .prove *.broker.log */*.broker.log *.output python/__pycache__

#  Put long running tests here. They will be included first
#   in TESTS so that `make check` runs them first, hopefully resulting
#   in a reduced makespan overall.
LONGTESTSCRIPTS = \
	t9000-system.t \
	t5000-valgrind.t \
	t3100-flux-in-flux.t \
	t3200-instance-restart.t \
	t3202-instance-restart-testexec.t \
	t3203-instance-recovery.t \
	t4000-issues-test-driver.t \
	t2801-top-cmd.t \
	t2808-shutdown-cmd.t \
	t2712-python-cli-alloc.t \
	t2714-python-cli-batch.t

# This list is included in both TESTS and dist_check_SCRIPTS.
TESTSCRIPTS = \
	$(LONGTESTSCRIPTS) \
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0004-event.t \
	t0005-exec.t \
	t0005-rexec.t \
	t0005-exec-jobid.t \
	t0007-ping.t \
	t0008-attr.t \
	t0009-dmesg.t \
	t0010-generic-utils.t \
	t0011-content-cache.t \
	t0012-content-sqlite.t \
	t0024-content-s3.t \
	t0028-content-backing-none.t \
	t0029-archive-mmap.t \
	t0030-marshall.t \
	t0031-constraint-parser.t \
	t0032-directives-parser.t \
	t0033-filemap-cmd.t \
	t0025-broker-state-machine.t \
	t0027-broker-groups.t \
	t0013-config-file.t \
	t0014-runlevel.t \
	t0015-cron.t \
	t0016-cron-faketime.t \
	t0017-security.t \
	t0018-content-files.t \
	t0019-tbon-config.t \
	t0020-terminus.t \
	t0021-archive-cmd.t \
	t0022-jj-reader.t \
	t0023-jobspec1-validate.t \
	t0026-flux-R.t \
	t0090-content-enospc.t \
	t1000-kvs.t \
	t1001-kvs-internals.t \
	t1003-kvs-stress.t \
	t1004-kvs-namespace.t \
	t1005-kvs-security.t \
	t1007-kvs-lookup-watch.t \
	t1008-kvs-eventlog.t \
	t1009-kvs-copy.t \
	t1010-kvs-commit-sync.t \
	t1011-kvs-checkpoint-period.t \
	t1101-barrier-basic.t \
	t1102-cmddriver.t \
	t1103-apidisconnect.t \
	t1105-proxy.t \
	t1106-ssh-connector.t \
	t1107-heartbeat.t \
	t1200-stats-basic.t \
	t2004-hydra.t \
	t2008-althash.t \
	t2010-kvs-snapshot-restore.t \
	t2100-job-ingest.t \
	t2110-job-ingest-validator.t \
	t2111-job-ingest-config.t \
	t2112-job-ingest-frobnicator.t \
	t2113-job-ingest-pipeline.t \
	t2201-job-cmd.t \
	t2202-job-manager.t \
	t2203-job-manager-single.t \
	t2204-job-manager-limited.t \
	t2205-job-manager-unlimited.t \
	t2206-job-manager-annotate.t \
	t2208-job-manager-wait.t \
	t2209-job-manager-bugs.t \
	t2210-job-manager-events-journal.t \
	t2211-job-manager-jobspec.t \
	t2212-job-manager-plugins.t \
	t2213-job-manager-hold-single.t \
	t2214-job-manager-hold-limited.t \
	t2215-job-manager-hold-unlimited.t \
	t2216-job-manager-priority-order-single.t \
	t2217-job-manager-priority-order-limited.t \
	t2218-job-manager-priority-order-unlimited.t \
	t2219-job-manager-restart.t \
	t2220-job-manager-R.t \
	t2221-job-manager-limit-duration.t \
	t2222-job-manager-limit-job-size.t \
	t2223-job-manager-queue-priority-order-limited.t \
	t2224-job-manager-queue-priority-order-unlimited.t \
	t2226-housekeeping.t \
	t2230-job-info-lookup.t \
	t2231-job-info-eventlog-watch.t \
	t2232-job-info-security.t \
	t2233-job-info-update.t \
	t2240-queue-cmd.t \
	t2241-queue-cmd-list.t \
	t2245-policy-config.t \
	t2260-job-list.t \
	t2261-job-list-update.t \
	t2262-job-list-stats.t \
	t2270-job-dependencies.t \
	t2271-job-dependency-after.t \
	t2272-job-begin-time.t \
	t2273-job-alloc-bypass.t \
	t2274-manager-perilog-per-rank.t \
	t2275-job-duration-validator.t \
	t2276-job-requires.t \
	t2280-job-memo.t \
	t2290-job-update.t \
	t2291-job-update-queue.t \
	t2292-job-update-running.t \
	t2300-sched-simple.t \
	t2302-sched-simple-up-down.t \
	t2303-sched-hello.t \
	t2304-sched-simple-alloc-check.t \
	t2305-sched-slow.t \
	t2310-resource-module.t \
	t2311-resource-drain.t \
	t2312-resource-exclude.t \
	t2313-resource-acquire.t \
	t2314-resource-monitor.t \
	t2315-resource-system.t \
	t2316-resource-rediscover.t \
	t2350-resource-list.t \
	t2351-resource-status-input.t \
	t2352-resource-cmd-config.t \
	t2353-resource-eventlog.t \
	t2354-resource-status.t \
	t2355-resource-journal.t \
	t2400-job-exec-test.t \
	t2401-job-exec-hello.t \
	t2402-job-exec-dummy.t \
	t2403-job-exec-conf.t \
	t2404-job-exec-multiuser.t \
	t2406-job-exec-cleanup.t \
	t2407-sdbus.t \
	t2408-sdbus-recovery.t \
	t2409-sdexec.t \
	t2410-sdexec-memlimit.t \
	t2411-sdexec-job.t \
	t2500-job-attach.t \
	t2501-job-status.t \
	t2600-job-shell-rcalc.t \
	t2602-job-shell.t \
	t2603-job-shell-initrc.t \
	t2604-job-shell-affinity.t \
	t2606-job-shell-output-redirection.t \
	t2607-job-shell-input.t \
	t2608-job-shell-log.t \
	t2609-job-shell-events.t \
	t2610-job-shell-mpir.t \
	t2611-debug-emulate.t \
	t2612-job-shell-pty.t \
	t2613-job-shell-batch.t \
	t2614-job-shell-doom.t \
	t2615-job-shell-rlimit.t \
	t2616-job-shell-taskmap.t \
	t2616-job-shell-taskmap-hostfile.t \
	t2617-job-shell-stage-in.t \
	t2618-job-shell-signal.t \
	t2619-job-shell-hwloc.t \
	t2620-job-shell-mustache.t \
	t2710-python-cli-submit.t \
	t2711-python-cli-run.t \
	t2713-python-cli-bulksubmit.t \
	t2715-python-cli-cancel.t \
	t2716-python-cli-batch-conf.t \
	t2800-jobs-cmd.t \
	t2800-jobs-recursive.t \
	t2800-jobs-instance-info.t \
	t2800-jobs-config.t \
	t2802-uri-cmd.t \
	t2803-flux-pstree.t \
	t2804-uptime-cmd.t \
	t2805-startlog-cmd.t \
	t2806-config-cmd.t \
	t2807-dump-cmd.t \
	t2809-job-purge.t \
	t2810-kvs-garbage-collect.t \
	t2811-flux-pgrep.t \
	t2812-flux-job-last.t \
	t2813-flux-watch.t \
	t2814-hostlist-cmd.t \
	t2815-post-job-event.t \
	t2900-job-timelimits.t \
	t3000-mpi-basic.t \
	t3001-mpi-personalities.t \
	t3002-pmi.t \
	t3003-mpi-abort.t \
	t3201-crontabs.t \
	t3300-system-basic.t \
	t3301-system-latestart.t \
	t3302-system-offline.t \
	t3303-system-healthcheck.t \
	t3304-system-rpctrack-down.t \
	t3305-system-rpctrack-up.t \
	t3306-system-routercrash.t \
	t3307-system-leafcrash.t \
	t3308-system-torpid.t \
	t3309-system-reconnect.t \
	t3400-overlay-trace.t \
	t3401-module-trace.t \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0004-getattr.t \
	lua/t0007-alarm.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1004-statwatcher.t \
	python/t0001-handle.py \
	python/t0002-wrapper.py \
	python/t0003-barrier.py \
	python/t0004-event.py \
	python/t0005-kvs.py \
	python/t0006-request.py \
	python/t0007-watchers.py \
	python/t0008-message.py \
	python/t0010-job.py \
	python/t0012-futures.py \
	python/t0013-job-list.py \
	python/t0014-job-kvslookup.py \
	python/t0015-job-output.py \
	python/t0020-hostlist.py \
	python/t0021-idset.py \
	python/t0022-resource-set.py \
	python/t0023-executor.py \
	python/t0024-util.py \
	python/t0025-uri.py \
	python/t0026-tree.py \
	python/t0027-constraint-parser.py \
	python/t0028-compat36.py \
	python/t0029-fileref.py \
	python/t0030-journal.py \
	python/t0031-conf-builtin.py \
	python/t0032-resource-journal.py \
	python/t0033-eventlog-formatter.py \
	python/t1000-service-add-remove.py

if HAVE_FLUX_SECURITY
TESTSCRIPTS += python/t0009-security.py
endif

# make check runs these TAP tests directly (both scripts and programs)
TESTS = $(TESTSCRIPTS)

EXTRA_DIST= \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T) \
	test-under-flux/expected.modcheck \
	test-under-flux/t_modcheck.t \
	test-under-flux/test.t \
	rc/rc1-kvs \
	rc/rc1-job \
	rc/rc3-kvs \
	rc/rc3-job \
	shell/input \
	shell/output \
	shell/initrc/tests \
	flux-jobs/tests \
	scripts/strerror_symbol \
	scripts/run_timeout.py \
	scripts/startctl.py \
	scripts/groups.py \
	scripts/rexec.py \
	jobspec \
	resource \
	marshall \
	batch \
	job-manager/dumps \
	flux-resource

dist_check_SCRIPTS = \
	$(TESTSCRIPTS) \
	system/0001-basic.t \
	system/0002-exec-with-imp.t \
	system/0004-recovery.t \
	system/0005-exec.t \
	issues/t0441-kvs-put-get.sh \
	issues/t0505-msg-handler-reg.lua \
	issues/t0821-kvs-segfault.sh \
	issues/t1760-kvs-use-after-free.sh \
	issues/t2281-service-add-crash.sh \
	issues/t2284-initial-program-format-chars.sh \
	issues/t2686-shell-input-race.sh \
	issues/t3186-python-future-get-sigint.sh \
	issues/t3415-job-shell-segfault-on-null.sh \
	issues/t3432-python-sigint.sh \
	issues/t3429-python-future-ref.py \
	issues/t3470-multithread-reactor-run.py \
	issues/t3503-nofiles-limit.sh \
	issues/t3775-binary-io.sh \
	issues/t3920-running-underflow.sh \
	issues/t3960-job-exec-ehostunreach.sh \
	issues/t3906-job-exec-exception.sh \
	issues/t3982-python-message-ref.py \
	issues/t4182-resource-rerank.sh \
	issues/t4184-sched-simple-restart.sh \
	issues/t4222-kvs-assume-empty-dir.sh \
	issues/t4375-content-flush-hang.sh \
	issues/t4378-content-flush-force.sh \
	issues/t4379-dirty-cache-entries-flush.sh \
	issues/t4413-empty-eventlog.sh \
	issues/t4465-job-list-use-after-free.sh \
	issues/t4482-flush-list-corruption.sh \
	issues/t4583-free-range-test.sh \
	issues/t4612-eventlog-overwrite-crash.sh \
	issues/t4711-job-list-purge-inactive.sh \
	issues/t4852-t_submit-legacy.sh \
	issues/t5105-signal-propagation.sh \
	issues/t5308-kvsdir-initial-path.py \
	issues/t5368-kvs-commit-clear.py \
	issues/t5657-kvs-getroot-namespace.sh \
	issues/t5892-shutdown-no-epilog.sh \
	issues/t2492-shell-lost.sh \
	python/__init__.py \
	python/subflux.py \
	python/tap \
	scripts/event-trace.lua \
	scripts/event-trace-bypass.lua \
	scripts/waitfile.lua \
	scripts/t0004-event-helper.sh \
	scripts/tssh \
	scripts/sign-as.py \
	scripts/runpty.py \
	scripts/dmesg-grep.py \
	scripts/stats-listen.py \
	scripts/sqlite-query.py \
	scripts/pipe.py \
	valgrind/valgrind-workload.sh \
	valgrind/workload.d/job \
	valgrind/workload.d/job-info \
	valgrind/workload.d/job-multinode \
	valgrind/workload.d/job-wait \
	valgrind/workload.d/job-cancel \
	valgrind/workload.d/job-list \
	valgrind/workload.d/job-sdexec \
	valgrind/workload.d/resource \
	content/content-helper.sh \
	kvs/kvs-helper.sh \
	kvs/change-checkpoint.py \
	job-manager/exec-service.lua \
	job-manager/drain-cancel.py \
	job-manager/bulk-state.py \
	job-manager/submit-wait.py \
	job-manager/submit-waitany.py \
	job-manager/submit-sliding-window.py \
	job-manager/wait-interrupted.py \
	job-manager/sched-helper.sh \
	job-manager/job-conv.py \
	job-attach/outputsleep.sh \
	job-exec/dummy.sh \
	job-exec/imp.sh \
	job-exec/imp-fail.sh \
	job-list/list-id.py \
	job-list/list-rpc.py \
	job-list/job-list-helper.sh \
	ingest/bad-validate.py

check_PROGRAMS = \
	loop/logstderr \
	loop/issue2337 \
	loop/issue2711 \
	kvs/content-spam \
	kvs/torture \
	kvs/dtree \
	kvs/blobref \
	kvs/watch_disconnect \
	kvs/watch_stream \
	kvs/commit \
	kvs/transactionmerge \
	kvs/lookup_invalid \
	kvs/commit_order \
	kvs/issue1760 \
	kvs/issue1876 \
	kvs/waitcreate_cancel \
	kvs/setrootevents \
	request/treq \
	request/rpc \
	request/rpc_stream \
	barrier/tbarrier \
	reactor/reactorcat \
	rexec/rexec \
	rexec/rexec_count_stdout \
	rexec/rexec_getline \
	job-manager/list-jobs \
	job-manager/print-constants \
	job-manager/events_journal_stream \
	job-info/info_lookup \
	job-info/update_lookup \
	job-info/update_watch_stream \
	ingest/submitbench \
	sched-simple/jj-reader \
	shell/rcalc \
	shell/mpir \
	debug/stall \
	hwloc/hwloc-convert \
	hwloc/hwloc-version \
	util/jobspec1-validate \
	util/handle \
	util/marshall

if HAVE_MPI
check_PROGRAMS += \
	mpi/hello \
	mpi/abort \
	mpi/version \
	mpi/mpich_basic/self \
	mpi/mpich_basic/simple \
	mpi/mpich_basic/sendrecv \
	mpi/mpich_basic/srtest \
	mpi/mpich_basic/netpipe \
	mpi/mpich_basic/patterns \
	mpi/mpich_basic/adapt
endif

check_LTLIBRARIES = \
	module/testmod.la \
	module/running.la \
	module/legacy.la \
	request/req.la \
	ingest/job-manager.la \
	disconnect/watcher.la \
	shell/plugins/dummy.la \
	shell/plugins/conftest.la \
	shell/plugins/invalid-args.la \
	shell/plugins/getopt.la \
	shell/plugins/log.la \
	shell/plugins/test-event.la \
	shell/plugins/jobspec-info.la \
	shell/plugins/taskmap-reverse.la \
	job-manager/plugins/priority-wait.la \
	job-manager/plugins/priority-invert.la \
	job-manager/plugins/args.la \
	job-manager/plugins/test.la \
	job-manager/plugins/job_aux.la \
	job-manager/plugins/jobtap_api.la \
	job-manager/plugins/random.la \
	job-manager/plugins/validate.la \
	job-manager/plugins/dependency-test.la \
	job-manager/plugins/subscribe.la \
	job-manager/plugins/cleanup-event.la \
	job-manager/plugins/create-event.la \
	job-manager/plugins/create-reject.la \
	job-manager/plugins/perilog-test.la \
	job-manager/plugins/config.la \
	job-manager/plugins/jobspec-update.la \
	job-manager/plugins/jobspec-update-job-list.la \
	job-manager/plugins/resource-update-expiration.la \
	job-manager/plugins/update-test.la \
	job-manager/plugins/project-bank-validate.la \
	job-manager/plugins/offline.la \
	stats/stats-basic.la \
	stats/stats-immediate.la

dist_check_DATA = \
	hwloc-data/sierra2/0.xml \
	hwloc-data/sierra2/1.xml \
	hwloc-data/corona/0.xml \
	hwloc-data/1N/shared/02-brokers/0.xml \
	hwloc-data/1N/shared/02-brokers/1.xml \
	hwloc-data/1N/nonoverlapping/02-brokers/0.xml \
	hwloc-data/1N/nonoverlapping/02-brokers/1.xml \
	hwloc-data/1N/hwloc-versions/v1.11.11/0.xml \
	hwloc-data/1N/hwloc-versions/v2.1.0/0.xml \
	hwloc-data/1N/hwloc-versions/v2to1/0.xml \
	valgrind/valgrind.supp

test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBPTHREAD)

test_ldflags = \
	-no-install

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

loop_logstderr_SOURCES = loop/logstderr.c
loop_logstderr_CPPFLAGS = $(test_cppflags)
loop_logstderr_LDADD = $(test_ldadd)
loop_logstderr_LDFLAGS = $(test_ldflags)

loop_issue2337_SOURCES = loop/issue2337.c
loop_issue2337_CPPFLAGS = $(test_cppflags)
loop_issue2337_LDADD = $(test_ldadd)
loop_issue2337_LDFLAGS = $(test_ldflags)

loop_issue2711_SOURCES = loop/issue2711.c
loop_issue2711_CPPFLAGS = $(test_cppflags)
loop_issue2711_LDADD = $(test_ldadd)
loop_issue2711_LDFLAGS = $(test_ldflags)

mpi_hello_SOURCES = mpi/hello.c
mpi_hello_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_hello_LDADD = $(MPI_CLDFLAGS) $(test_ldadd)
mpi_hello_LDFLAGS = $(test_ldflags)

mpi_abort_SOURCES = mpi/abort.c
mpi_abort_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_abort_LDADD = $(MPI_CLDFLAGS) $(test_ldadd)
mpi_abort_LDFLAGS = $(test_ldflags)

mpi_version_SOURCES = mpi/version.c
mpi_version_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_version_LDADD = $(MPI_CLDFLAGS) $(test_ldadd)
mpi_version_LDFLAGS = $(test_ldflags)

mpi_mpich_basic_adapt_SOURCES = mpi/mpich_basic/adapt.c mpi/mpich_basic/GetOpt.c mpi/mpich_basic/GetOpt.h
mpi_mpich_basic_adapt_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_mpich_basic_adapt_LDADD = $(test_ldadd)
mpi_mpich_basic_adapt_LDFLAGS = $(MPI_CLDFLAGS) $(test_ldflags)

mpi_mpich_basic_netpipe_SOURCES = mpi/mpich_basic/netmpi.c mpi/mpich_basic/GetOpt.c mpi/mpich_basic/GetOpt.h
mpi_mpich_basic_netpipe_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_mpich_basic_netpipe_LDADD = $(test_ldadd)
mpi_mpich_basic_netpipe_LDFLAGS = $(MPI_CLDFLAGS) $(test_ldflags)

mpi_mpich_basic_patterns_SOURCES = mpi/mpich_basic/patterns.c
mpi_mpich_basic_patterns_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_mpich_basic_patterns_LDADD = $(test_ldadd)
mpi_mpich_basic_patterns_LDFLAGS = $(MPI_CLDFLAGS) $(test_ldflags)

mpi_mpich_basic_srtest_SOURCES = mpi/mpich_basic/srtest.c
mpi_mpich_basic_srtest_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_mpich_basic_srtest_LDADD = $(test_ldadd)
mpi_mpich_basic_srtest_LDFLAGS = $(MPI_CLDFLAGS) $(test_ldflags)

mpi_mpich_basic_self_SOURCES = mpi/mpich_basic/self.c
mpi_mpich_basic_self_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_mpich_basic_self_LDADD = $(MPI_CLDFLAGS) $(test_ldadd)
mpi_mpich_basic_self_LDFLAGS = $(MPI_CLDFLAGS) $(test_ldflags)

mpi_mpich_basic_sendrecv_SOURCES = mpi/mpich_basic/sendrecv.c
mpi_mpich_basic_sendrecv_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_mpich_basic_sendrecv_LDADD = $(MPI_CLDFLAGS) $(test_ldadd)
mpi_mpich_basic_sendrecv_LDFLAGS = $(MPI_CLDFLAGS) $(test_ldflags)

mpi_mpich_basic_simple_SOURCES = mpi/mpich_basic/simple.c
mpi_mpich_basic_simple_CPPFLAGS = $(MPI_CFLAGS) $(test_cppflags)
mpi_mpich_basic_simple_LDADD = $(MPI_CLDFLAGS) $(test_ldadd)
mpi_mpich_basic_simple_LDFLAGS = $(MPI_CLDFLAGS) $(test_ldflags)

kvs_content_spam_SOURCES = kvs/content-spam.c
kvs_content_spam_CPPFLAGS = $(test_cppflags)
kvs_content_spam_LDADD = $(test_ldadd)
kvs_content_spam_LDFLAGS = $(test_ldflags)

kvs_torture_SOURCES = kvs/torture.c
kvs_torture_CPPFLAGS = $(test_cppflags)
kvs_torture_LDADD = $(test_ldadd)
kvs_torture_LDFLAGS = $(test_ldflags)

kvs_dtree_SOURCES = kvs/dtree.c
kvs_dtree_CPPFLAGS = $(test_cppflags)
kvs_dtree_LDADD = $(test_ldadd)
kvs_dtree_LDFLAGS = $(test_ldflags)

kvs_blobref_SOURCES = kvs/blobref.c
kvs_blobref_CPPFLAGS = $(test_cppflags)
kvs_blobref_LDADD = $(test_ldadd)
kvs_blobref_LDFLAGS = $(test_ldflags)

kvs_commit_SOURCES = kvs/commit.c
kvs_commit_CPPFLAGS = $(test_cppflags)
kvs_commit_LDADD = $(test_ldadd)
kvs_commit_LDFLAGS = $(test_ldflags)

kvs_transactionmerge_SOURCES = kvs/transactionmerge.c
kvs_transactionmerge_CPPFLAGS = $(test_cppflags)
kvs_transactionmerge_LDADD = $(test_ldadd)
kvs_transactionmerge_LDFLAGS = $(test_ldflags)

kvs_lookup_invalid_SOURCES = kvs/lookup_invalid.c
kvs_lookup_invalid_CPPFLAGS = $(test_cppflags)
kvs_lookup_invalid_LDADD = $(test_ldadd)
kvs_lookup_invalid_LDFLAGS = $(test_ldflags)

kvs_commit_order_SOURCES = kvs/commit_order.c
kvs_commit_order_CPPFLAGS = $(test_cppflags)
kvs_commit_order_LDADD = $(test_ldadd)
kvs_commit_order_LDFLAGS = $(test_ldflags)

kvs_watch_disconnect_SOURCES = kvs/watch_disconnect.c
kvs_watch_disconnect_CPPFLAGS = $(test_cppflags)
kvs_watch_disconnect_LDADD = $(test_ldadd)
kvs_watch_disconnect_LDFLAGS = $(test_ldflags)

kvs_watch_stream_SOURCES = kvs/watch_stream.c
kvs_watch_stream_CPPFLAGS = $(test_cppflags)
kvs_watch_stream_LDADD = $(test_ldadd)
kvs_watch_stream_LDFLAGS = $(test_ldflags)

kvs_issue1760_SOURCES = kvs/issue1760.c
kvs_issue1760_CPPFLAGS = $(test_cppflags)
kvs_issue1760_LDADD = $(test_ldadd)
kvs_issue1760_LDFLAGS = $(test_ldflags)

kvs_issue1876_SOURCES = kvs/issue1876.c
kvs_issue1876_CPPFLAGS = $(test_cppflags)
kvs_issue1876_LDADD = $(test_ldadd)
kvs_issue1876_LDFLAGS = $(test_ldflags)

kvs_waitcreate_cancel_SOURCES = kvs/waitcreate_cancel.c
kvs_waitcreate_cancel_CPPFLAGS = $(test_cppflags)
kvs_waitcreate_cancel_LDADD = $(test_ldadd)
kvs_waitcreate_cancel_LDFLAGS = $(test_ldflags)

kvs_setrootevents_SOURCES = kvs/setrootevents.c
kvs_setrootevents_CPPFLAGS = $(test_cppflags)
kvs_setrootevents_LDADD = $(test_ldadd)
kvs_setrootevents_LDFLAGS = $(test_ldflags)

request_treq_SOURCES = request/treq.c
request_treq_CPPFLAGS = $(test_cppflags)
request_treq_LDADD = $(test_ldadd)
request_treq_LDFLAGS = $(test_ldflags)

request_rpc_SOURCES = request/rpc.c
request_rpc_CPPFLAGS = $(test_cppflags)
request_rpc_LDADD = $(test_ldadd)
request_rpc_LDFLAGS = $(test_ldflags)

request_rpc_stream_SOURCES = request/rpc_stream.c
request_rpc_stream_CPPFLAGS = $(test_cppflags)
request_rpc_stream_LDADD = $(test_ldadd)
request_rpc_stream_LDFLAGS = $(test_ldflags)

module_testmod_la_SOURCES = module/testmod.c
module_testmod_la_CPPFLAGS = $(test_cppflags)
module_testmod_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
module_testmod_la_LIBADD = $(test_ldadd) $(LIBDL)

module_running_la_SOURCES = module/running.c
module_running_la_CPPFLAGS = $(test_cppflags)
module_running_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
module_running_la_LIBADD = $(test_ldadd)

module_legacy_la_SOURCES = module/legacy.c
module_legacy_la_CPPFLAGS = $(test_cppflags)
module_legacy_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
module_legacy_la_LIBADD = $(test_ldadd) $(LIBDL)

barrier_tbarrier_SOURCES = barrier/tbarrier.c
barrier_tbarrier_CPPFLAGS = $(test_cppflags)
barrier_tbarrier_LDADD = $(test_ldadd)
barrier_tbarrier_LDFLAGS = $(test_ldflags)

request_req_la_SOURCES = request/req.c
request_req_la_CPPFLAGS = $(test_cppflags)
request_req_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
request_req_la_LIBADD = $(test_ldadd)

shell_rcalc_SOURCES = shell/rcalc.c
shell_rcalc_CPPFLAGS = $(test_cppflags)
shell_rcalc_LDADD = $(top_builddir)/src/shell/libshell.la \
	$(test_ldadd)
shell_rcalc_LDFLAGS = $(test_ldflags)

shell_mpir_SOURCES = shell/mpir.c
shell_mpir_CPPFLAGS = $(test_cppflags)
shell_mpir_LDADD = \
	$(top_builddir)/src/shell/libmpir.la \
	$(top_builddir)/src/cmd/job/mpir.o \
	$(top_builddir)/src/common/libdebugged/libdebugged.la \
	$(test_ldadd)
shell_mpir_LDFLAGS = $(test_ldflags)

debug_stall_SOURCES = debug/stall.c
debug_stall_CPPFLAGS = $(test_cppflags)

reactor_reactorcat_SOURCES = reactor/reactorcat.c
reactor_reactorcat_CPPFLAGS = $(test_cppflags)
reactor_reactorcat_LDADD = $(test_ldadd)
reactor_reactorcat_LDFLAGS = \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(test_ldflags) \
	$(top_builddir)/src/common/libflux/libflux.la

rexec_rexec_SOURCES = rexec/rexec.c
rexec_rexec_CPPFLAGS = $(test_cppflags)
rexec_rexec_LDADD = $(test_ldadd)
rexec_rexec_LDFLAGS = \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(test_ldflags) \
	$(top_builddir)/src/common/libflux/libflux.la

rexec_rexec_count_stdout_SOURCES = rexec/rexec_count_stdout.c
rexec_rexec_count_stdout_CPPFLAGS = $(test_cppflags)
rexec_rexec_count_stdout_LDADD = $(test_ldadd)
rexec_rexec_count_stdout_LDFLAGS = $(test_ldflags)

rexec_rexec_getline_SOURCES = rexec/rexec_getline.c
rexec_rexec_getline_CPPFLAGS = $(test_cppflags)
rexec_rexec_getline_LDADD = $(test_ldadd)
rexec_rexec_getline_LDFLAGS = \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(test_ldflags) \
	$(top_builddir)/src/common/libflux/libflux.la

ingest_job_manager_la_SOURCES = ingest/job-manager.c
ingest_job_manager_la_CPPFLAGS = $(test_cppflags)
ingest_job_manager_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere
ingest_job_manager_la_LIBADD = $(test_ldadd)

ingest_submitbench_SOURCES = ingest/submitbench.c
ingest_submitbench_CPPFLAGS = $(test_cppflags)
ingest_submitbench_LDADD = $(test_ldadd)
ingest_submitbench_LDFLAGS = $(test_ldflags)

job_manager_list_jobs_SOURCES = job-manager/list-jobs.c
job_manager_list_jobs_CPPFLAGS = $(test_cppflags)
job_manager_list_jobs_LDADD = $(test_ldadd)
job_manager_list_jobs_LDFLAGS = $(test_ldflags)

job_manager_print_constants_SOURCES = job-manager/print-constants.c
job_manager_print_constants_CPPFLAGS = $(test_cppflags)
job_manager_print_constants_LDADD = $(test_ldadd)
job_manager_print_constants_LDFLAGS = $(test_ldflags)

job_manager_events_journal_stream_SOURCES = job-manager/events_journal_stream.c
job_manager_events_journal_stream_CPPFLAGS = $(test_cppflags)
job_manager_events_journal_stream_LDADD = $(test_ldadd)
job_manager_events_journal_stream_LDFLAGS = $(test_ldflags)

job_info_info_lookup_SOURCES = job-info/info_lookup.c
job_info_info_lookup_CPPFLAGS = $(test_cppflags)
job_info_info_lookup_LDADD = $(test_ldadd)
job_info_info_lookup_LDFLAGS = $(test_ldflags)

job_info_update_lookup_SOURCES = job-info/update_lookup.c
job_info_update_lookup_CPPFLAGS = $(test_cppflags)
job_info_update_lookup_LDADD = $(test_ldadd)
job_info_update_lookup_LDFLAGS = $(test_ldflags)

job_info_update_watch_stream_SOURCES = job-info/update_watch_stream.c
job_info_update_watch_stream_CPPFLAGS = $(test_cppflags)
job_info_update_watch_stream_LDADD = $(test_ldadd)
job_info_update_watch_stream_LDFLAGS = $(test_ldflags)

disconnect_watcher_la_SOURCES = disconnect/watcher.c
disconnect_watcher_la_CPPFLAGS = $(test_cppflags)
disconnect_watcher_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere
disconnect_watcher_la_LIBADD = $(test_ldadd)

sched_simple_jj_reader_SOURCES = sched-simple/jj-reader.c
sched_simple_jj_reader_CPPFLAGS = $(test_cppflags)
sched_simple_jj_reader_LDADD = \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(test_ldadd)
sched_simple_jj_reader_LDFLAGS = $(test_ldflags)

shell_plugins_dummy_la_SOURCES = shell/plugins/dummy.c
shell_plugins_dummy_la_CPPFLAGS = $(test_cppflags)
shell_plugins_dummy_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere

shell_plugins_conftest_la_SOURCES = shell/plugins/conftest.c
shell_plugins_conftest_la_CPPFLAGS = $(test_cppflags)
shell_plugins_conftest_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere

shell_plugins_invalid_args_la_SOURCES = shell/plugins/invalid-args.c
shell_plugins_invalid_args_la_CPPFLAGS = $(test_cppflags)
shell_plugins_invalid_args_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
shell_plugins_invalid_args_la_LIBADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-core.la

shell_plugins_getopt_la_SOURCES = shell/plugins/getopt.c
shell_plugins_getopt_la_CPPFLAGS = $(test_cppflags)
shell_plugins_getopt_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
shell_plugins_getopt_la_LIBADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-core.la

shell_plugins_log_la_SOURCES = shell/plugins/log.c
shell_plugins_log_la_CPPFLAGS = $(test_cppflags)
shell_plugins_log_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
shell_plugins_log_la_LIBADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-core.la

shell_plugins_test_event_la_SOURCES = shell/plugins/test-event.c
shell_plugins_test_event_la_CPPFLAGS = $(test_cppflags)
shell_plugins_test_event_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
shell_plugins_test_event_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

shell_plugins_jobspec_info_la_SOURCES = shell/plugins/jobspec-info.c
shell_plugins_jobspec_info_la_CPPFLAGS = $(test_cppflags)
shell_plugins_jobspec_info_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
shell_plugins_jobspec_info_la_LIBADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-core.la

shell_plugins_taskmap_reverse_la_SOURCES = shell/plugins/taskmap-reverse.c
shell_plugins_taskmap_reverse_la_CPPFLAGS = $(test_cppflags)
shell_plugins_taskmap_reverse_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
shell_plugins_taskmap_reverse_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_priority_wait_la_SOURCES = \
	job-manager/plugins/priority-wait.c
job_manager_plugins_priority_wait_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_priority_wait_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_priority_wait_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_priority_invert_la_SOURCES = \
	job-manager/plugins/priority-invert.c
job_manager_plugins_priority_invert_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_priority_invert_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_priority_invert_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_args_la_SOURCES = \
	job-manager/plugins/args.c
job_manager_plugins_args_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_args_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_args_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_subscribe_la_SOURCES = \
	job-manager/plugins/subscribe.c
job_manager_plugins_subscribe_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_subscribe_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_subscribe_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_test_la_SOURCES = \
	job-manager/plugins/test.c
job_manager_plugins_test_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_test_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_test_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_job_aux_la_SOURCES = \
	job-manager/plugins/job_aux.c
job_manager_plugins_job_aux_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_job_aux_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_job_aux_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_jobtap_api_la_SOURCES = \
	job-manager/plugins/jobtap_api.c
job_manager_plugins_jobtap_api_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_jobtap_api_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_jobtap_api_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_random_la_SOURCES = \
	job-manager/plugins/random.c
job_manager_plugins_random_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_random_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_random_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la


job_manager_plugins_validate_la_SOURCES = \
	job-manager/plugins/validate.c
job_manager_plugins_validate_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_validate_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_validate_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_dependency_test_la_SOURCES = \
	job-manager/plugins/dependency-test.c
job_manager_plugins_dependency_test_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_dependency_test_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_dependency_test_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_cleanup_event_la_SOURCES = \
	job-manager/plugins/cleanup-event.c
job_manager_plugins_cleanup_event_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_cleanup_event_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_cleanup_event_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_create_event_la_SOURCES = \
	job-manager/plugins/create-event.c
job_manager_plugins_create_event_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_create_event_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_create_event_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_create_reject_la_SOURCES = \
	job-manager/plugins/create-reject.c
job_manager_plugins_create_reject_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_create_reject_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_create_reject_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_config_la_SOURCES = \
	job-manager/plugins/config.c
job_manager_plugins_config_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_config_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_config_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_perilog_test_la_SOURCES = \
	job-manager/plugins/perilog-test.c
job_manager_plugins_perilog_test_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_perilog_test_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_perilog_test_la_LIBADD = \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_jobspec_update_la_SOURCES = \
	job-manager/plugins/jobspec-update.c
job_manager_plugins_jobspec_update_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_jobspec_update_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_jobspec_update_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_jobspec_update_job_list_la_SOURCES = \
	job-manager/plugins/jobspec-update-job-list.c
job_manager_plugins_jobspec_update_job_list_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_jobspec_update_job_list_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_jobspec_update_job_list_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_resource_update_expiration_la_SOURCES = \
	job-manager/plugins/resource-update-expiration.c
job_manager_plugins_resource_update_expiration_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_resource_update_expiration_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_resource_update_expiration_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_update_test_la_SOURCES = \
	job-manager/plugins/update-test.c
job_manager_plugins_update_test_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_update_test_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_update_test_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_project_bank_validate_la_SOURCES = \
	job-manager/plugins/project-bank-validate.c
job_manager_plugins_project_bank_validate_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_project_bank_validate_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_project_bank_validate_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

job_manager_plugins_offline_la_SOURCES = \
	job-manager/plugins/offline.c
job_manager_plugins_offline_la_CPPFLAGS = \
	$(test_cppflags)
job_manager_plugins_offline_la_LDFLAGS = \
        $(fluxplugin_ldflags) -module -rpath /nowhere
job_manager_plugins_offline_la_LIBADD = \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la

hwloc_hwloc_convert_SOURCES = hwloc/hwloc-convert.c
hwloc_hwloc_convert_CPPFLAGS = $(HWLOC_CFLAGS) $(test_cppflags)
hwloc_hwloc_convert_LDADD = $(HWLOC_LIBS) \
	$(test_ldadd)
hwloc_hwloc_convert_LDFLAGS = $(test_ldadd)

hwloc_hwloc_version_SOURCES = hwloc/hwloc-version.c
hwloc_hwloc_version_CPPFLAGS = $(HWLOC_CFLAGS) $(test_cppflags)
hwloc_hwloc_version_LDADD = $(HWLOC_LIBS) \
	$(test_ldadd)
hwloc_hwloc_version_LDFLAGS = $(test_ldadd)

util_jobspec1_validate_SOURCES = util/jobspec1-validate.c
util_jobspec1_validate_CPPFLAGS = $(test_cppflags)
util_jobspec1_validate_LDADD = $(test_ldadd)
util_jobspec1_validate_LDFLAGS = $(test_ldflags)

util_handle_SOURCES = util/handle.c
util_handle_CPPFLAGS = $(test_cppflags)
util_handle_LDADD = $(test_ldadd)
util_handle_LDFLAGS = $(test_ldflags)

util_marshall_SOURCES = util/marshall.c
util_marshall_CPPFLAGS = $(test_cppflags)
util_marshall_LDADD = $(test_ldadd)
util_marshall_LDFLAGS = $(test_ldflags)

stats_stats_basic_la_SOURCES = stats/stats-basic.c
stats_stats_basic_la_CPPFLAGS = $(test_cppflags)
stats_stats_basic_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
stats_stats_basic_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

stats_stats_immediate_la_SOURCES = stats/stats-immediate.c
stats_stats_immediate_la_CPPFLAGS = $(test_cppflags)
stats_stats_immediate_la_LDFLAGS = \
	$(fluxplugin_ldflags) -module -rpath /nowhere
stats_stats_immediate_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
