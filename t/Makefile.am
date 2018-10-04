AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
        -I$(top_srcdir) -I$(top_srcdir)/src/include \
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
	export PYTHONPATH="$(top_builddir)/src/bindings/python:$(top_srcdir)/src/bindings/python:$(top_srcdir)/t/python/tap:$(PYTHON_SITE_PKG):$$PYTHONPATH";

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

TESTS = \
	shmem/backtoback.t \
	loop/handle.t \
	loop/dispatch.t \
	loop/reactor.t \
	loop/reduce.t \
	loop/log.t \
	rpc/rpc.t \
	rpc/mrpc.t \
	rolemask/loop.t \
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
	t1000-kvs.t \
	t1001-kvs-internals.t \
	t1002-kvs-watch.t \
	t1003-kvs-stress.t \
	t1004-kvs-namespace.t \
	t1005-kvs-security.t \
	t1006-kvs-getroot.t \
	t1007-kvs-lookup-watch.t \
	t1008-kvs-eventlog.t \
	t1101-barrier-basic.t \
	t1102-cmddriver.t \
	t1103-apidisconnect.t \
	t1104-kz.t \
	t1105-proxy.t \
	t1999-wreck-rcalc.t \
	t2000-wreck.t \
	t2000-wreck-env.t \
	t2000-wreck-dummy-sched.t \
	t2000-wreck-epilog.t \
	t2001-jsc.t \
	t2002-pmi.t \
	t2003-recurse.t \
	t2004-hydra.t \
	t2005-hwloc-basic.t \
	t2006-joblog.t \
	t2007-caliper.t \
	t2008-althash.t \
	t2009-hostlist.t \
	t2100-aggregate.t \
	t2200-job-ingest.t \
	t2201-job-cmd.t \
	t3000-mpi-basic.t \
        t3001-mpi-personalities.t \
	t4000-issues-test-driver.t \
	t5000-valgrind.t \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0004-getattr.t \
	lua/t0007-alarm.t \
	lua/t0009-sequences.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1002-kvs.t \
	lua/t1003-iowatcher.t \
	lua/t1004-statwatcher.t \
	lua/t1005-fdwatcher.t

if ENABLE_JOBSPEC
TESTS += \
	t0018-jobspec.t
endif

if HAVE_PYTHON
TESTS += \
	python/t0001-handle.py \
	python/t0002-wrapper.py \
	python/t0003-barrier.py \
	python/t0004-event.py \
	python/t0005-kvs.py \
	python/t0006-request.py \
	python/t0007-watchers.py \
	python/t0008-jsc.py
endif

EXTRA_DIST= \
	$(check_SCRIPTS) \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T) \
	rc/rc1-kvs \
	rc/rc1-wreck \
	rc/rc1-testenv \
	rc/rc1-job \
	rc/rc3-kvs \
	rc/rc3-wreck \
	rc/rc3-testenv \
	rc/rc3-job \
	wreck/input \
	wreck/output \
	jobspec

clean-local:
	rm -fr trash-directory.* test-results .prove *.broker.log */*.broker.log *.output python/__pycache__

check_SCRIPTS = \
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
	t1000-kvs.t \
	t1001-kvs-internals.t \
	t1002-kvs-watch.t \
	t1003-kvs-stress.t \
	t1004-kvs-namespace.t \
	t1005-kvs-security.t \
	t1006-kvs-getroot.t \
	t1007-kvs-lookup-watch.t \
	t1008-kvs-eventlog.t \
	t1101-barrier-basic.t \
	t1102-cmddriver.t \
	t1103-apidisconnect.t \
	t1104-kz.t \
	t1105-proxy.t \
	t1999-wreck-rcalc.t \
	t2000-wreck.t \
	t2000-wreck-env.t \
	t2000-wreck-dummy-sched.t \
	t2000-wreck-epilog.t \
	t2001-jsc.t \
	t2002-pmi.t \
	t2003-recurse.t \
	t2004-hydra.t \
	t2005-hwloc-basic.t \
	t2006-joblog.t \
	t2007-caliper.t \
	t2008-althash.t \
	t2009-hostlist.t \
	t2100-aggregate.t \
	t2200-job-ingest.t \
	t2201-job-cmd.t \
	t3000-mpi-basic.t \
        t3001-mpi-personalities.t \
	t4000-issues-test-driver.t \
	t5000-valgrind.t \
        issues/t0441-kvs-put-get.sh \
        issues/t0505-msg-handler-reg.lua \
        issues/t0821-kvs-segfault.sh \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0004-getattr.t \
	lua/t0007-alarm.t \
	lua/t0009-sequences.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1002-kvs.t \
	lua/t1003-iowatcher.t \
	lua/t1004-statwatcher.t \
	lua/t1005-fdwatcher.t \
	t0018-jobspec.t \
	python/__init__.py \
	python/subflux.py \
	python/tap \
	python/t0001-handle.py \
	python/t0002-wrapper.py \
	python/t0003-barrier.py \
	python/t0004-event.py \
	python/t0005-kvs.py \
	python/t0006-request.py \
	python/t0007-watchers.py \
	python/t0008-jsc.py

check_PROGRAMS = \
	shmem/backtoback.t \
	loop/handle.t \
	loop/dispatch.t \
	loop/reactor.t \
	loop/reduce.t \
	loop/log.t \
	loop/logstderr \
	rpc/rpc.t \
	rpc/mrpc.t \
	rolemask/loop.t \
	kz/kzcopy \
	kvs/torture \
	kvs/dtree \
	kvs/blobref \
	kvs/hashtest \
	kvs/watch \
	kvs/watch_disconnect \
	kvs/commit \
	kvs/transactionmerge \
	kvs/fence_namespace_remove \
	kvs/fence_invalid \
	kvs/commit_order \
	module/basic \
	request/treq \
	barrier/tbarrier \
	wreck/rcalc \
	reactor/reactorcat \
	rexec/rexec \
	rexec/rexec_signal \
	rexec/rexec_ps

check_LTLIBRARIES = \
	module/parent.la \
	module/child.la \
	request/req.la \
	wreck/sched-dummy.la

if HAVE_MPI
check_PROGRAMS += \
	mpi/hello
endif

dist_check_DATA = \
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

dist_check_SCRIPTS = \
	scripts/event-trace.lua \
	scripts/event-trace-bypass.lua \
	scripts/kvs-watch-until.lua \
	scripts/kvs-get-ex.lua \
	scripts/cpus-allowed.lua \
	scripts/waitfile.lua \
	scripts/t0004-event-helper.sh \
	scripts/tssh \
	valgrind/valgrind-workload.sh \
	kvs/kvs-helper.sh

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

loop_handle_t_SOURCES = loop/handle.c
loop_handle_t_CPPFLAGS = $(test_cppflags)
loop_handle_t_LDADD = $(test_ldadd) $(LIBDL)

loop_dispatch_t_SOURCES = loop/dispatch.c
loop_dispatch_t_CPPFLAGS = $(test_cppflags)
loop_dispatch_t_LDADD = $(test_ldadd) $(LIBDL)

loop_log_t_SOURCES = loop/log.c
loop_log_t_CPPFLAGS = $(test_cppflags)
loop_log_t_LDADD = $(test_ldadd) $(LIBDL)

loop_logstderr_SOURCES = loop/logstderr.c
loop_logstderr_CPPFLAGS = $(test_cppflags)
loop_logstderr_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

loop_reactor_t_SOURCES = loop/reactor.c
loop_reactor_t_CPPFLAGS = $(test_cppflags)
loop_reactor_t_LDADD = $(test_ldadd) $(LIBDL)

loop_reduce_t_SOURCES = loop/reduce.c
loop_reduce_t_CPPFLAGS = $(test_cppflags)
loop_reduce_t_LDADD = $(test_ldadd) $(LIBDL)

rpc_rpc_t_SOURCES = rpc/rpc.c rpc/util.h rpc/util.c
rpc_rpc_t_CPPFLAGS = $(test_cppflags)
rpc_rpc_t_LDADD = $(test_ldadd) $(LIBDL)

rpc_mrpc_t_SOURCES = rpc/mrpc.c rpc/util.h rpc/util.c
rpc_mrpc_t_CPPFLAGS = $(test_cppflags)
rpc_mrpc_t_LDADD = $(test_ldadd) $(LIBDL)

rolemask_loop_t_SOURCES = rolemask/loop.c
rolemask_loop_t_CPPFLAGS = $(test_cppflags)
rolemask_loop_t_LDADD = $(test_ldadd) $(LIBDL)

mpi_hello_SOURCES = mpi/hello.c
mpi_hello_CPPFLAGS = $(MPI_CFLAGS)
mpi_hello_LDADD = $(MPI_CLDFLAGS) $(LIBRT)

kz_kzcopy_SOURCES = kz/kzcopy.c
kz_kzcopy_CPPFLAGS = $(test_cppflags)
kz_kzcopy_LDADD = $(test_ldadd) $(LIBDL) $(LIBUTIL)

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

kvs_watch_SOURCES = kvs/watch.c
kvs_watch_CPPFLAGS = $(test_cppflags)
kvs_watch_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_commit_SOURCES = kvs/commit.c
kvs_commit_CPPFLAGS = $(test_cppflags)
kvs_commit_LDADD = \
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

request_treq_SOURCES = request/treq.c
request_treq_CPPFLAGS = $(test_cppflags)
request_treq_LDADD = \
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

module_basic_SOURCES = module/basic.c
module_basic_CPPFLAGS = $(test_cppflags)
module_basic_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

request_req_la_SOURCES = request/req.c
request_req_la_CPPFLAGS = $(test_cppflags)
request_req_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
request_req_la_LIBADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

wreck_rcalc_SOURCES = wreck/rcalc.c
wreck_rcalc_CPPFLAGS = $(test_cppflags)
wreck_rcalc_LDADD = \
        $(test_ldadd) $(LIBDL) $(LIBUTIL) \
	$(top_builddir)/src/modules/wreck/rcalc.o

wreck_sched_dummy_la_SOURCES = wreck/sched-dummy.c
wreck_sched_dummy_la_CPPFLAGS = $(test_cppflags)
wreck_sched_dummy_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
wreck_sched_dummy_la_LIBADD = \
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
