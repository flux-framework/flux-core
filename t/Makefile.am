AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
        $(JSON_CFLAGS) $(ZMQ_CFLAGS) \
        -I$(top_srcdir) -I$(top_srcdir)/src/include

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

TESTS = \
	loop/handle.t \
	loop/reactor.t \
	loop/rpc.t \
	loop/multrpc.t \
	loop/reduce.t \
	pmi/simple.t \
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0004-event.t \
	t0005-exec.t \
	t0006-build-basic.t \
	t0007-ping.t \
	t0008-attr.t \
	t0009-dmesg.t \
	t0010-generic-utils.t \
	t0011-content-cache.t \
	t0012-content-sqlite.t \
	t0013-content-sophia.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t \
	t1002-modctl.t \
	t1003-mecho.t \
	t1005-cmddriver.t \
	t1006-apidisconnect.t \
	t1007-kz.t \
	t2000-wreck.t \
	t2001-jsc.t \
	t2002-pmi.t \
	t2003-recurse.t \
	t2004-hydra.t \
	t3000-mpi-basic.t \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0007-alarm.t \
	lua/t0008-mrpc.t \
	lua/t0009-sequences.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1002-kvs.t \
	lua/t1003-iowatcher.t \
	lua/t1004-statwatcher.t \
	lua/t1005-fdwatcher.t

EXTRA_DIST= \
	$(check_SCRIPTS) \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T)

clean-local:
	rm -fr trash-directory.* test-results .prove *.broker.log */*.broker.log *.output

check_SCRIPTS = \
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0004-event.t \
	t0005-exec.t \
	t0006-build-basic.t \
	t0007-ping.t \
	t0008-attr.t \
	t0009-dmesg.t \
	t0010-generic-utils.t \
	t0011-content-cache.t \
	t0012-content-sqlite.t \
	t0013-content-sophia.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t \
	t1002-modctl.t \
	t1003-mecho.t \
	t1005-cmddriver.t \
	t1006-apidisconnect.t \
	t1007-kz.t \
	t2000-wreck.t \
	t2001-jsc.t \
	t2002-pmi.t \
	t2003-recurse.t \
	t2004-hydra.t \
	t3000-mpi-basic.t \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0007-alarm.t \
	lua/t0008-mrpc.t \
	lua/t0009-sequences.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1002-kvs.t \
	lua/t1003-iowatcher.t \
	lua/t1004-statwatcher.t \
	lua/t1005-fdwatcher.t

check_PROGRAMS = \
	loop/handle.t \
	loop/reactor.t \
	loop/rpc.t \
	loop/multrpc.t \
	loop/reduce.t \
	pmi/kvstest \
	pmi/simple.t \
	pmi/pminfo \
	kz/kzutil \
	kvs/torture \
	kvs/watch

if HAVE_MPI
check_PROGRAMS += \
	mpi/hello
endif

dist_check_DATA = \
	build/hello.mk \
	build/hello_czmq.c \
	build/hello_flux_core.c \
	build/hello_flux_internal.c \
	build/hello_jsonc.c

dist_check_SCRIPTS = \
	scripts/event-trace.lua \
	scripts/kvs-watch-until.lua \
	scripts/cpus-allowed.lua \
	scripts/waitfile.lua \
	scripts/t0004-event-helper.sh

test_ldadd = \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD)

test_cppflags = \
        -DMODULE_PATH=\"$(top_builddir)/src/modules\" \
        -DCONNECTOR_PATH=\"$(top_builddir)/src/connectors\" \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

loop_handle_t_SOURCES = loop/handle.c
loop_handle_t_CPPFLAGS = $(test_cppflags)
loop_handle_t_LDADD = $(test_ldadd) $(LIBDL)

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

pmi_kvstest_SOURCES = pmi/kvstest.c
pmi_kvstest_CPPFLAGS = $(test_cppflags)
pmi_kvstest_LDADD = $(test_ldadd) $(LIBRT)

pmi_simple_t_SOURCES = pmi/simple.c
pmi_simple_t_CPPFLAGS = $(test_cppflags)
pmi_simple_t_LDADD = $(test_ldadd) $(LIBRT)

pmi_pminfo_SOURCES = pmi/pminfo.c
pmi_pminfo_CPPFLAGS = $(test_cppflags)
pmi_pminfo_LDADD = $(test_ldadd) $(LIBRT)

mpi_hello_SOURCES = mpi/hello.c
mpi_hello_CPPFLAGS = $(MPI_CFLAGS)
mpi_hello_LDADD = $(MPI_CLDFLAGS) $(LIBRT)

kz_kzutil_SOURCES = kz/kzutil.c
kz_kzutil_CPPFLAGS = $(test_cppflags)
kz_kzutil_LDADD = \
	$(top_builddir)/src/modules/libkz/libkz.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_torture_SOURCES = kvs/torture.c
kvs_torture_CPPFLAGS = $(test_cppflags)
kvs_torture_LDADD = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)

kvs_watch_SOURCES = kvs/watch.c
kvs_watch_CPPFLAGS = $(test_cppflags)
kvs_watch_LDADD = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(test_ldadd) $(LIBDL) $(LIBUTIL)
