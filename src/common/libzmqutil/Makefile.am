AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
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
	zwatcher.h \
	zwatcher.c \
	zap.h \
	zap.c \
	monitor.h \
	monitor.c \
	sockopt.h \
	sockopt.c \
	mpart.h \
	mpart.c \
	cert.h \
	cert.c

TESTS = test_msg_zsock.t \
	test_zwatcher.t \
	test_zap.t \
	test_monitor.t \
	test_mpart.t \
	test_cert.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(LIBUUID_LIBS) \
	$(ZMQ_LIBS)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	-I$(srcdir) \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

test_msg_zsock_t_SOURCES = test/msg_zsock.c
test_msg_zsock_t_CPPFLAGS = $(test_cppflags)
test_msg_zsock_t_LDADD = $(test_ldadd)
test_msg_zsock_t_LDFLAGS = $(test_ldflags)

test_zwatcher_t_SOURCES = test/zwatcher.c
test_zwatcher_t_CPPFLAGS = $(test_cppflags)
test_zwatcher_t_LDADD = $(test_ldadd)
test_zwatcher_t_LDFLAGS = $(test_ldflags)

test_zap_t_SOURCES = test/zap.c
test_zap_t_CPPFLAGS = $(test_cppflags)
test_zap_t_LDADD = $(test_ldadd)
test_zap_t_LDFLAGS = $(test_ldflags)

test_monitor_t_SOURCES = test/monitor.c
test_monitor_t_CPPFLAGS = $(test_cppflags)
test_monitor_t_LDADD = $(test_ldadd)
test_monitor_t_LDFLAGS = $(test_ldflags)

test_mpart_t_SOURCES = test/mpart.c
test_mpart_t_CPPFLAGS = $(test_cppflags)
test_mpart_t_LDADD = $(test_ldadd)
test_mpart_t_LDFLAGS = $(test_ldflags)

test_cert_t_SOURCES = test/cert.c
test_cert_t_CPPFLAGS = $(test_cppflags)
test_cert_t_LDADD = $(test_ldadd)
test_cert_t_LDFLAGS = $(test_ldflags)
