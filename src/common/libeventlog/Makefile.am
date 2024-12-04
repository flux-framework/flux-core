AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libeventlog.la
libeventlog_la_SOURCES = \
	eventlog.h \
	eventlog.c \
	eventlogger.h \
	eventlogger.c \
	formatter.h \
	formatter.c

TESTS = \
	test_eventlog.t \
	test_formatter.t

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
	$(JANSSON_LIBS) \
	$(LIBRT)

test_formatter_t_SOURCES = test/formatter.c
test_formatter_t_CPPFLAGS = $(AM_CPPFLAGS)
test_formatter_t_LDADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libeventlog/libeventlog.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libmissing/libmissing.la \
	$(JANSSON_LIBS) \
	$(LIBRT)
