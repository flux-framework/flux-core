AM_CFLAGS = @GCCWARN@

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
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0005-exec.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t \
	t1002-modctl.t \
	t1003-mecho.t \
	t1004-log.t \
	t1005-cmddriver.t \
	t1006-apidisconnect.t \
	t2000-wreck.t \
	t2001-jsc.t \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0007-alarm.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1002-kvs.t \
	lua/t1003-iowatcher.t

EXTRA_DIST= \
	$(check_SCRIPTS) \
	aggregate-results.sh \
	sharness.sh \
	sharness.d \
	$(T)

clean-local:
	rm -fr trash-directory.* test-results .prove *.broker.log */*.broker.log

check_SCRIPTS = \
	t0000-sharness.t \
	t0001-basic.t \
	t0002-request.t \
	t0003-module.t \
	t0005-exec.t \
	t1000-kvs-basic.t \
	t1001-barrier-basic.t \
	t1002-modctl.t \
	t1003-mecho.t \
	t1004-log.t \
	t1005-cmddriver.t \
	t1006-apidisconnect.t \
	t2000-wreck.t \
	t2001-jsc.t \
	lua/t0001-send-recv.t \
	lua/t0002-rpc.t \
	lua/t0003-events.t \
	lua/t0007-alarm.t \
	lua/t1000-reactor.t \
	lua/t1001-timeouts.t \
	lua/t1002-kvs.t \
	lua/t1003-iowatcher.t

check_PROGRAMS = \
	loop/handle.t \
	loop/reactor.t \
	loop/rpc.t \
	loop/multrpc.t

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
