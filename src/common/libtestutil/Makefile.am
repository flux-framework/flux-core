AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(LIBUUID_CFLAGS)

check_LTLIBRARIES = libtestutil.la

libtestutil_la_SOURCES = \
	util.c \
	util.h \
	util_multi.c \
	util_multi.h \
	test_file.c \
	test_file.h \
	util_rpc.c \
	util_rpc.h

TESTS = test_multirank.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	libtestutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(LIBPTHREAD) \
	$(LIBUUID_LIBS)

test_ldflags = \
	-no-install

test_cppflags = \
	$(AM_CPPFLAGS)

test_multirank_t_SOURCES = test/multirank.c
test_multirank_t_CPPFLAGS = $(test_cppflags)
test_multirank_t_LDFLAGS = $(test_ldflags)
test_multirank_t_LDADD = $(test_ldadd)
