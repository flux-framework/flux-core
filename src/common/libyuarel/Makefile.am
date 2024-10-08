AM_CFLAGS = \
        -I$(top_srcdir) \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS)

noinst_LTLIBRARIES = libyuarel.la

libyuarel_la_SOURCES = \
	yuarel.c \
	yuarel.h

TESTS = test_yuarel.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_cppflags = \
        $(AM_CPPFLAGS)

test_ldadd = \
        $(top_builddir)/src/common/libyuarel/libyuarel.la \
        $(top_builddir)/src/common/libtap/libtap.la

test_yuarel_t_SOURCES = test/test_lib.c test/minunit.h
test_yuarel_t_LDADD = $(test_ldadd)
test_yuarel_t_CPPFLAGS = $(test_cppflags)

EXTRA_DIST = examples/simple.c README.md LICENSE
