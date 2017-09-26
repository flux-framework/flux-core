AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = liboptparse.la
fluxinclude_HEADERS = optparse.h
liboptparse_la_SOURCES = optparse.c getopt.c getopt_int.h getopt.h

liboptparse_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
liboptparse_la_LDFLAGS = \
	-avoid-version -module -shared -export-dynamic \
	$(AM_LDFLAGS)


TESTS = test_optparse.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_optparse_t_SOURCES = test/optparse.c
test_optparse_t_CPPFLAGS = $(AM_CPPFLAGS)
test_optparse_t_LDADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liboptparse/liboptparse.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(ZMQ_LIBS) $(LIBPTHREAD)
