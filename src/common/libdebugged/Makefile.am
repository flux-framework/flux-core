AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
        -I$(top_srcdir)

noinst_LTLIBRARIES = libdebugged.la
libdebugged_la_SOURCES = debugged.c debugged.h

TESTS = test_debugged.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_debugged_t_SOURCES = test/test_debugged.c
test_debugged_t_CPPFLAGS = $(AM_CPPFLAGS)
test_debugged_t_LDADD = \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(top_builddir)/src/common/libdebugged/libdebugged.la
