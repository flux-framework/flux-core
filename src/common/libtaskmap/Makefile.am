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
	-I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = \
	libtaskmap.la
fluxinclude_HEADERS = \
	taskmap.h
libtaskmap_la_SOURCES = \
	taskmap_private.h \
	taskmap.c

TESTS = test_taskmap.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_taskmap_t_SOURCES = test/taskmap.c
test_taskmap_t_CPPFLAGS = $(AM_CPPFLAGS)
test_taskmap_t_LDADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libtaskmap/libtaskmap.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(JANSSON_LIBS)
