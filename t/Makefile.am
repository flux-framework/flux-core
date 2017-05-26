AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
        -I$(top_srcdir) -I$(top_srcdir)/src/include \
        $(ZMQ_CFLAGS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

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
	loop/reactor.t \
	loop/rpc.t \
	loop/multrpc.t \
	loop/reduce.t \
	loop/log.t \
	rolemask/loop.t \
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0004-event.t \
	t0005-exec.t \
	t0007-ping.t \
	t0008-attr.t \
	t0009-dmesg.t \
	t0010-generic-utils.t \
	t0011-content-cache.t \
	t0012-content-sqlite.t \
	t0014-runlevel.t \
	t0015-cron.t \
	t0016-cron-faketime.t \
	t0017-security.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t \
	t1002-kvs-cmd.t \
	t1005-cmddriver.t \
	t1006-apidisconnect.t \
	t1007-kz.t \
	t1008-proxy.t \
	t2000-wreck.t \
	t2001-jsc.t \
	t2002-pmi.t \
	t2003-recurse.t \
	t2004-hydra.t \
	t2005-hwloc-basic.t \
	t2006-joblog.t \
	t2007-caliper.t \
	t2008-althash.t \
	t2100-aggregate.t \
	t3000-mpi-basic.t \
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

if HAVE_PYTHON
TESTS += \
	 $(top_builddir)/t/t9990-python-tests.t
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
	rc/rc3-kvs \
	rc/rc3-wreck \
	rc/rc3-testenv

clean-local:
	rm -fr trash-directory.* test-results .prove *.broker.log */*.broker.log *.output

check_SCRIPTS = \
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0004-event.t \
	t0005-exec.t \
	t0007-ping.t \
	t0008-attr.t \
	t0009-dmesg.t \
	t0010-generic-utils.t \
	t0011-content-cache.t \
	t0012-content-sqlite.t \
	t0014-runlevel.t \
	t0015-cron.t \
	t0016-cron-faketime.t \
	t0017-security.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t \
	t1002-kvs-cmd.t \
	t1005-cmddriver.t \
	t1006-apidisconnect.t \
	t1007-kz.t \
	t1008-proxy.t \
	t2000-wreck.t \
	t2001-jsc.t \
	t2002-pmi.t \
	t2003-recurse.t \
	t2004-hydra.t \
	t2005-hwloc-basic.t \
	t2006-joblog.t \
	t2007-caliper.t \
	t2008-althash.t \
	t2100-aggregate.t \
	t3000-mpi-basic.t \
	t4000-issues-test-driver.t \
	t5000-valgrind.t \
        issues/t0441-kvs-put-get.sh \
        issues/t0505-msg-handler-reg.lua \
        issues/t0821-kvs-segfault.sh \
        issues/t0900-wreck-invalid-cores.sh \
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
	$(top_builddir)/t/t9990-python-tests.t

check_PROGRAMS = \
	shmem/backtoback.t \
	loop/handle.t \
	loop/reactor.t \
	loop/rpc.t \
	loop/multrpc.t \
	loop/reduce.t \
	loop/log.t \
	rolemask/loop.t \
	kz/kzutil \
	kvs/torture \
	kvs/dtree \
	kvs/getas \
	kvs/blobref \
	kvs/asyncfence \
	kvs/hashtest \
	kvs/watch \
	kvs/watch_disconnect \
	kvs/commit \
	kvs/commitmerge \
	kvs/basic \
	module/basic \
	request/treq

check_LTLIBRARIES = \
	module/parent.la \
	module/child.la \
	request/req.la \
	request/coproc.la

if HAVE_MPI
check_PROGRAMS += \
	mpi/hello
endif

dist_check_DATA = \
	hwloc-data/1N/shared/02-brokers/0.xml \
	hwloc-data/1N/shared/02-brokers/1.xml \
	hwloc-data/1N/nonoverlapping/02-brokers/0.xml \
	hwloc-data/1N/nonoverlapping/02-brokers/1.xml \
	valgrind/valgrind.supp

dist_check_SCRIPTS = \
	scripts/event-trace.lua \
	scripts/event-trace-bypass.lua \
	scripts/kvs-watch-until.lua \
	scripts/kvs-get-ex.lua \
	scripts/cpus-allowed.lua \
	scripts/waitfile.lua \
	scripts/t0004-event-helper.sh \
	scripts/tssh \
	valgrind/valgrind-workload.sh

test_ldadd = \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la \
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

loop_log_t_SOURCES = loop/log.c
loop_log_t_CPPFLAGS = $(test_cppflags)
loop_log_t_LDADD = $(test_ldadd) $(LIBDL)

loop_reactor_t_SOURCES = loop/reactor.c
loop_reactor_t_CPPFLAGS = $(test_cppflags)
loop_reactor_t_LDADD = $(test_ldadd) $(LIBDL)

loop_rpc_t_SOURCES = loop/rpc.c
loop_rpc_t_CPPFLAGS = $(test_cppflags)
loop_rpc_t_LDADD = $(test_ldadd) $(LIBDL)

loop_multrpc_t_SOURCES = loop/multrpc.c
loop_multrpc_t_CPPFLAGS = $(test_cppflags)
loop_multrpc_t_LDADD = $(test_ldadd) $(LIBDL)

loop_reduce_t_SOURCES = loop/reduce.c
loop_reduce_t_CPPFLAGS = $(test_cppflags)
loop_reduce_t_LDADD = $(test_ldadd) $(LIBDL)

rolemask_loop_t_SOURCES = rolemask/loop.c
rolemask_loop_t_CPPFLAGS = $(test_cppflags)
rolemask_loop_t_LDADD = $(test_ldadd) $(LIBDL)

mpi_hello_SOURCES = mpi/hello.c
mpi_hello_CPPFLAGS = $(MPI_CFLAGS)
mpi_hello_LDADD = $(MPI_CLDFLAGS) $(LIBRT)

kz_kzutil_SOURCES = kz/kzutil.c
kz_kzutil_CPPFLAGS = $(test_cppflags)
kz_kzutil_LDADD = \
	$(top_builddir)/src/modules/libkz/libkz.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_torture_SOURCES = kvs/torture.c
kvs_torture_CPPFLAGS = $(test_cppflags)
kvs_torture_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_dtree_SOURCES = kvs/dtree.c
kvs_dtree_CPPFLAGS = $(test_cppflags)
kvs_dtree_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_getas_SOURCES = kvs/getas.c
kvs_getas_CPPFLAGS = $(test_cppflags)
kvs_getas_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_blobref_SOURCES = kvs/blobref.c
kvs_blobref_CPPFLAGS = $(test_cppflags)
kvs_blobref_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_asyncfence_SOURCES = kvs/asyncfence.c
kvs_asyncfence_CPPFLAGS = $(test_cppflags)
kvs_asyncfence_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_watch_SOURCES = kvs/watch.c
kvs_watch_CPPFLAGS = $(test_cppflags)
kvs_watch_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_commit_SOURCES = kvs/commit.c
kvs_commit_CPPFLAGS = $(test_cppflags)
kvs_commit_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_commitmerge_SOURCES = kvs/commitmerge.c
kvs_commitmerge_CPPFLAGS = $(test_cppflags)
kvs_commitmerge_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_watch_disconnect_SOURCES = kvs/watch_disconnect.c
kvs_watch_disconnect_CPPFLAGS = $(test_cppflags)
kvs_watch_disconnect_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(top_builddir)/src/modules/kvs/proto.o \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_hashtest_SOURCES = kvs/hashtest.c
kvs_hashtest_CPPFLAGS = $(test_cppflags) $(SQLITE_CFLAGS)
kvs_hashtest_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL) $(LIBJUDY) $(SQLITE_LIBS)

kvs_basic_SOURCES = kvs/basic.c
kvs_basic_CPPFLAGS = $(test_cppflags)
kvs_basic_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

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

module_basic_SOURCES = module/basic.c
module_basic_CPPFLAGS = $(test_cppflags)
module_basic_LDADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

request_coproc_la_SOURCES = request/coproc.c
request_coproc_la_CPPFLAGS = $(test_cppflags)
request_coproc_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
request_coproc_la_LIBADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

request_req_la_SOURCES = request/req.c
request_req_la_CPPFLAGS = $(test_cppflags)
request_req_la_LDFLAGS = $(fluxmod_ldflags) -module -rpath /nowher
request_req_la_LIBADD = \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)
