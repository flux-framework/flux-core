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
	$(JANSSON_CFLAGS) \
	$(FLUX_SECURITY_CFLAGS)

noinst_LTLIBRARIES = libterminus.la

libterminus_la_SOURCES = \
	pty.c \
	pty.h \
	client.c \
	terminus.h \
	terminus.c

TESTS = \
	test_pty.t \
	test_terminus.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(top_builddir)/src/common/libterminus/libterminus.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtestutil/libtestutil.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libtap/libtap.la

test_ldflags = \
	-no-install

test_cppflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_pty_t_SOURCES = test/pty.c
test_pty_t_CPPFLAGS = $(test_cppflags)
test_pty_t_LDADD = $(test_ldadd)
test_pty_t_LDFLAGS = $(test_ldflags)

test_terminus_t_SOURCES = test/terminus.c
test_terminus_t_CPPFLAGS = $(test_cppflags)
test_terminus_t_LDADD = $(test_ldadd)
test_terminus_t_LDFLAGS = $(test_ldflags)
