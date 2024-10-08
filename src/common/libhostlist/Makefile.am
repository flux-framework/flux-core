AM_CFLAGS = \
    -I$(top_srcdir) \
    $(WARNING_CFLAGS) \
    $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
    $(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
    $(CODE_COVERAGE_CPPFLAGS)

noinst_LTLIBRARIES = \
    libhostlist.la

fluxinclude_HEADERS = \
    hostlist.h

libhostlist_la_SOURCES = \
    util.h \
    util.c \
    hostname.h \
    hostname.c \
    hostrange.h \
    hostrange.c \
    hostlist.h \
    hostlist.c

TESTS = \
    test_util.t \
    test_hostname.t \
    test_hostrange.t \
    test_hostlist.t

check_PROGRAMS = \
    $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_hostname_t_SOURCES = \
    test/hostname.c
test_hostname_t_CPPFLAGS = \
    $(AM_CPPFLAGS)
test_hostname_t_LDADD = \
    $(top_builddir)/src/common/libtap/libtap.la \
    $(top_builddir)/src/common/libhostlist/libhostlist.la

test_util_t_SOURCES = \
    test/util.c
test_util_t_CPPFLAGS = \
    $(AM_CPPFLAGS)
test_util_t_LDADD = \
    $(top_builddir)/src/common/libtap/libtap.la \
    $(top_builddir)/src/common/libhostlist/libhostlist.la

test_hostrange_t_SOURCES = \
    test/hostrange.c
test_hostrange_t_CPPFLAGS = \
    $(AM_CPPFLAGS)
test_hostrange_t_LDADD = \
    $(top_builddir)/src/common/libtap/libtap.la \
    $(top_builddir)/src/common/libhostlist/libhostlist.la

test_hostlist_t_SOURCES = \
    test/hostlist.c
test_hostlist_t_CPPFLAGS = \
    $(AM_CPPFLAGS)
test_hostlist_t_LDADD = \
    $(top_builddir)/src/common/libtap/libtap.la \
    $(top_builddir)/src/common/libhostlist/libhostlist.la
