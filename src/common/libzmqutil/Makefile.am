AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(LIBUUID_CFLAGS) \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = \
	libzmqutil.la

libzmqutil_la_SOURCES = \
	msg_zsock.h \
	msg_zsock.c \
	reactor.h \
	reactor.c \
	ev_zmq.h \
	ev_zmq.c \
	zap.h \
	zap.c \
	monitor.h \
	monitor.c \
	sockopt.h \
	sockopt.c

TESTS = test_msg_zsock.t \
	test_reactor.t \
	test_ev.t \
	test_zap.t \
	test_monitor.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(LIBUUID_LIBS) \
	$(ZMQ_LIBS)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

test_msg_zsock_t_SOURCES = test/msg_zsock.c
test_msg_zsock_t_CPPFLAGS = $(test_cppflags)
test_msg_zsock_t_LDADD = $(test_ldadd)
test_msg_zsock_t_LDFLAGS = $(test_ldflags)

test_reactor_t_SOURCES = test/reactor.c
test_reactor_t_CPPFLAGS = $(test_cppflags)
test_reactor_t_LDADD = $(test_ldadd)
test_reactor_t_LDFLAGS = $(test_ldflags)

test_ev_t_SOURCES = test/ev.c
test_ev_t_CPPFLAGS = $(test_cppflags)
test_ev_t_LDADD = $(test_ldadd)
test_ev_t_LDFLAGS = $(test_ldflags)

test_zap_t_SOURCES = test/zap.c
test_zap_t_CPPFLAGS = $(test_cppflags)
test_zap_t_LDADD = $(test_ldadd)
test_zap_t_LDFLAGS = $(test_ldflags)

test_monitor_t_SOURCES = test/monitor.c
test_monitor_t_CPPFLAGS = $(test_cppflags)
test_monitor_t_LDADD = $(test_ldadd)
test_monitor_t_LDFLAGS = $(test_ldflags)
