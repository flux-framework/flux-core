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

noinst_LTLIBRARIES = libeventlog.la
fluxinclude_HEADERS = eventlog.h
libeventlog_la_SOURCES = eventlog.c

libeventlog_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
libeventlog_la_LDFLAGS = \
	-avoid-version -module -shared -export-dynamic \
	$(AM_LDFLAGS)


TESTS = test_eventlog.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_eventlog_t_SOURCES = test/eventlog.c
test_eventlog_t_CPPFLAGS = $(AM_CPPFLAGS)
test_eventlog_t_LDADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libeventlog/libeventlog.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(JANSSON_LIBS)
