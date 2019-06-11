AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
        -I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
        $(ZMQ_CFLAGS)

#  If LUA_PATH is already set, ensure ./?.lua appears in LUA_PATH so that
#   fluxometer.lua is found. O/w, if LUA_PATH is *not* set, ./?.lua already
#   appears in the default LUA_PATH, so do nothing.
#
#  If we're testing with "installed" Flux, then also add path to Lua bindings
#   to LUA_CPATH, so that non-installed test modules can be found (at this
#   time, only lalarm.so)
#
AM_TESTS_ENVIRONMENT = \
	test -n "$$LUA_PATH" && export LUA_PATH="$(builddir)/?.lua;$$LUA_PATH";\
	test -n "$$FLUX_TEST_INSTALLED_PATH" && \
	export LUA_CPATH="$(abs_top_builddir)/src/bindings/lua/.libs/?.so;$$LUA_CPATH;;"; \
	export PYTHONPATH="$(abs_top_builddir)/src/bindings/python:$(abs_top_srcdir)/src/bindings/python:$(top_srcdir)/t/python/tap:$(PYTHON_SITE_PKG):$$PYTHONPATH";

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


# This list is included in both TESTS and dist_check_SCRIPTS.
TESTSCRIPTS = \
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0004-event.t \
	t0005-exec.t \
	t0005-rexec.t \
	t0007-ping.t \
	t0008-attr.t \
	t0009-dmesg.t \
	t0010-generic-utils.t \
	t0011-content-cache.t \
	t0012-content-sqlite.t \
	t0013-config-file.t \
	t0014-runlevel.t \
	t0015-cron.t \
	t0016-cron-faketime.t \
	t0017-security.t \
	t0019-jobspec-schema.t \
	t0020-emit-jobspec.t \
	t0021-flux-jobspec.t \
	t0022-jj-reader.t \
	t1000-kvs.t \
	t1001-kvs-internals.t \
	t1003-kvs-stress.t \
	t1004-kvs-namespace.t \
	t1005-kvs-security.t \
	t1007-kvs-lookup-watch.t \
	t1008-kvs-eventlog.t \
	t1009-kvs-copy.t \
	t1101-barrier-basic.t \
	t1102-cmddriver.t \
	t1103-apidisconnect.t \
	t1105-proxy.t \
	t2004-hydra.t \
	t2005-hwloc-basic.t \
	t2007-caliper.t \
	t2008-althash.t \
	t2010-kvs-snapshot-restore.t \
	t2100-aggregate.t \
	t2200-job-ingest.t \
	t2201-job-cmd.t \
	t2202-job-manager.t \
	t2203-job-manager-dummysched.t \
	t2204-job-info.t \
	t2205-job-info-security.t \
	t2206-job-manager-bulk-state.t \
	t2300-sched-simple.t \
	t2400-job-exec-test.t \
	t2401-job-exec-hello.t \
	t2500-job-attach.t \
	t4000-issues-test-driver.t \
	t5000-valgrind.t \
	t9001-pymod.t \
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
	python/t0010-job.py \
	python/t0011-mrpc.py \
	python/t0012-futures.py \
	python/t1000-service-add-remove.py

if HAVE_FLUX_SECURITY
TESTSCRIPTS += python/t0009-security.py
endif

# make check runs these TAP tests directly (both scripts and programs)
TESTS = \
	shmem/backtoback.t \
	$(TESTSCRIPTS)

EXTRA_DIST= \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T) \
	test-under-flux/expected.modcheck \
	test-under-flux/t_modcheck.t \
	test-under-flux/test.t \
	rc/rc1-kvs \
	rc/rc1-testenv \
	rc/rc1-job \
	rc/rc3-kvs \
	rc/rc3-testenv \
	rc/rc3-job \
	jobspec

dist_check_SCRIPTS = \
	$(TESTSCRIPTS) \
        issues/t0441-kvs-put-get.sh \
        issues/t0505-msg-handler-reg.lua \
        issues/t0821-kvs-segfault.sh \
	issues/t1760-kvs-use-after-free.sh \
	python/__init__.py \
	python/subflux.py \
	python/tap \
	scripts/event-trace.lua \
	scripts/event-trace-bypass.lua \
	scripts/waitfile.lua \
	scripts/t0004-event-helper.sh \
	scripts/tssh \
	valgrind/valgrind-workload.sh \
	valgrind/workload.d/job \
	kvs/kvs-helper.sh \
	job-manager/exec-service.lua \
	job-manager/drain-cancel.py \
	job-manager/drain-undrain.py \
	job-manager/bulk-state.py

check_PROGRAMS = \
	shmem/backtoback.t \
	loop/logstderr \
	kvs/torture \
	kvs/dtree \
	kvs/blobref \
	kvs/hashtest \
	kvs/watch_disconnect \
	kvs/commit \
	kvs/fence_api \
	kvs/transactionmerge \
	kvs/fence_namespace_remove \
	kvs/fence_invalid \
	kvs/lookup_invalid \
	kvs/commit_order \
	kvs/issue1760 \
	kvs/issue1876 \
	kvs/waitcreate_cancel \
	kvs/setrootevents \
	request/treq \
	request/rpc \
	barrier/tbarrier \
	reactor/reactorcat \
	rexec/rexec \
	rexec/rexec_signal \
	rexec/rexec_ps \
	ingest/submitbench \
	sched-simple/jj-reader

if HAVE_MPI
check_PROGRAMS += \
	mpi/hello
endif

check_LTLIBRARIES = \
	module/parent.la \
	module/child.la \
	request/req.la \
	ingest/job-manager-dummy.la \
	job-manager/sched-dummy.la

dist_check_DATA = \
	hwloc-data/sierra2/0.xml \
	hwloc-data/sierra2/1.xml \
	hwloc-data/1N/shared/02-brokers/0.xml \
	hwloc-data/1N/shared/02-brokers/1.xml \
	hwloc-data/1N/nonoverlapping/02-brokers/0.xml \
	hwloc-data/1N/nonoverlapping/02-brokers/1.xml \
	valgrind/valgrind.supp \
	conf.d/private.conf \
	conf.d/shared.conf \
	conf.d/shared_ipc.conf \
	conf.d/shared_none.conf \
	conf.d/bad-toml.conf \
	conf.d/bad-missing.conf \
	conf.d/bad-rank.conf \
	conf.d/priv2.0.conf \
	conf.d/priv2.1.conf

test_ldadd = \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-optparse.la \
        $(ZMQ_LIBS) $(LIBPTHREAD)

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

shmem_backtoback_t_SOURCES = shmem/backtoback.c
shmem_backtoback_t_CPPFLAGS = $(test_cppflags)
shmem_backtoback_t_LDADD = $(test_ldadd) $(LIBDL)

loop_logstderr_SOURCES = loop/logstderr.c
loop_logstderr_CPPFLAGS = $(test_cppflags)
loop_logstderr_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

mpi_hello_SOURCES = mpi/hello.c
mpi_hello_CPPFLAGS = $(MPI_CFLAGS)
mpi_hello_LDADD = $(MPI_CLDFLAGS) $(LIBRT)

kvs_torture_SOURCES = kvs/torture.c
kvs_torture_CPPFLAGS = $(test_cppflags)
kvs_torture_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_dtree_SOURCES = kvs/dtree.c
kvs_dtree_CPPFLAGS = $(test_cppflags)
kvs_dtree_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_blobref_SOURCES = kvs/blobref.c
kvs_blobref_CPPFLAGS = $(test_cppflags)
kvs_blobref_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_commit_SOURCES = kvs/commit.c
kvs_commit_CPPFLAGS = $(test_cppflags)
kvs_commit_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_fence_api_SOURCES = kvs/fence_api.c
kvs_fence_api_CPPFLAGS = $(test_cppflags)
kvs_fence_api_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_transactionmerge_SOURCES = kvs/transactionmerge.c
kvs_transactionmerge_CPPFLAGS = $(test_cppflags)
kvs_transactionmerge_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_fence_namespace_remove_SOURCES = kvs/fence_namespace_remove.c
kvs_fence_namespace_remove_CPPFLAGS = $(test_cppflags)
kvs_fence_namespace_remove_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_fence_invalid_SOURCES = kvs/fence_invalid.c
kvs_fence_invalid_CPPFLAGS = $(test_cppflags)
kvs_fence_invalid_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_lookup_invalid_SOURCES = kvs/lookup_invalid.c
kvs_lookup_invalid_CPPFLAGS = $(test_cppflags)
kvs_lookup_invalid_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_commit_order_SOURCES = kvs/commit_order.c
kvs_commit_order_CPPFLAGS = $(test_cppflags)
kvs_commit_order_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_watch_disconnect_SOURCES = kvs/watch_disconnect.c
kvs_watch_disconnect_CPPFLAGS = $(test_cppflags)
kvs_watch_disconnect_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_hashtest_SOURCES = kvs/hashtest.c
kvs_hashtest_CPPFLAGS = $(test_cppflags) $(SQLITE_CFLAGS)
kvs_hashtest_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL) $(LIBJUDY) $(SQLITE_LIBS)

kvs_issue1760_SOURCES = kvs/issue1760.c
kvs_issue1760_CPPFLAGS = $(test_cppflags)
kvs_issue1760_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_issue1876_SOURCES = kvs/issue1876.c
kvs_issue1876_CPPFLAGS = $(test_cppflags)
kvs_issue1876_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_waitcreate_cancel_SOURCES = kvs/waitcreate_cancel.c
kvs_waitcreate_cancel_CPPFLAGS = $(test_cppflags)
kvs_waitcreate_cancel_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_setrootevents_SOURCES = kvs/setrootevents.c
kvs_setrootevents_CPPFLAGS = $(test_cppflags)
kvs_setrootevents_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

request_treq_SOURCES = request/treq.c
request_treq_CPPFLAGS = $(test_cppflags)
request_treq_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

request_rpc_SOURCES = request/rpc.c
request_rpc_CPPFLAGS = $(test_cppflags)
request_rpc_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

module_parent_la_SOURCES = module/parent.c
module_parent_la_CPPFLAGS = $(test_cppflags)
module_parent_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
module_parent_la_LIBADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

module_child_la_SOURCES = module/child.c
module_child_la_CPPFLAGS = $(test_cppflags)
module_child_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
module_child_la_LIBADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

barrier_tbarrier_SOURCES = barrier/tbarrier.c
barrier_tbarrier_CPPFLAGS = $(test_cppflags)
barrier_tbarrier_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

request_req_la_SOURCES = request/req.c
request_req_la_CPPFLAGS = $(test_cppflags)
request_req_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
request_req_la_LIBADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

reactor_reactorcat_SOURCES = reactor/reactorcat.c
reactor_reactorcat_CPPFLAGS = $(test_cppflags)
reactor_reactorcat_LDADD = \
	 $(test_ldadd) $(LIBDL) $(LIBUTIL)

rexec_rexec_SOURCES = rexec/rexec.c
rexec_rexec_CPPFLAGS = $(test_cppflags)
rexec_rexec_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

rexec_rexec_signal_SOURCES = rexec/rexec_signal.c
rexec_rexec_signal_CPPFLAGS = $(test_cppflags)
rexec_rexec_signal_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

rexec_rexec_ps_SOURCES = rexec/rexec_ps.c
rexec_rexec_ps_CPPFLAGS = $(test_cppflags)
rexec_rexec_ps_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

ingest_job_manager_dummy_la_SOURCES = ingest/job-manager-dummy.c
ingest_job_manager_dummy_la_CPPFLAGS = $(test_cppflags)
ingest_job_manager_dummy_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere
ingest_job_manager_dummy_la_LIBADD = \
        $(test_ldadd) $(LIBDL) $(LIBUTIL)

ingest_submitbench_SOURCES = ingest/submitbench.c
ingest_submitbench_CPPFLAGS = $(test_cppflags)
ingest_submitbench_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

job_manager_sched_dummy_la_SOURCES = job-manager/sched-dummy.c
job_manager_sched_dummy_la_CPPFLAGS = $(test_cppflags)
job_manager_sched_dummy_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowhere
job_manager_sched_dummy_la_LIBADD = \
        $(top_builddir)/src/common/libschedutil/libschedutil.la \
        $(test_ldadd) $(LIBDL) $(LIBUTIL)

sched_simple_jj_reader_SOURCES = sched-simple/jj-reader.c
sched_simple_jj_reader_CPPFLAGS = $(test_cppflags)
sched_simple_jj_reader_LDADD = \
	$(top_builddir)/src/modules/sched-simple/libjj.la \
	$(test_ldadd)
