AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libidset.la
fluxinclude_HEADERS = idset.h
libidset_la_SOURCES = idset.c

libidset_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
libidset_la_LDFLAGS = \
	-avoid-version -module -shared -export-dynamic \
	$(AM_LDFLAGS)


TESTS = test_idset.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_idset_t_SOURCES = test/idset.c
test_idset_t_CPPFLAGS = $(AM_CPPFLAGS)
test_idset_t_LDADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(top_builddir)/src/common/libutil/libutil.la
