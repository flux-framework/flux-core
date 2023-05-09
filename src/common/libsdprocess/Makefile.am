AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = \
	libsdprocess.la

libsdprocess_la_SOURCES = \
	sdprocess.c \
	sdprocess.h \
	sdprocess_private.h \
	strv.c \
	strv.h

libsdprocess_la_LIBADD = \
	$(LIBSYSTEMD_LIBS)

TESTS = \
	test_sdprocess.t \
	test_strv.t

check_PROGRAMS = \
	$(TESTS) \
	test_echo \
	test_env

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(top_builddir)/src/common/libsdprocess/libsdprocess.la \
	$(top_builddir)/src/common/libtestutil/libtestutil.la \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS)

test_ldflags = \
	-no-install

test_cppflags = \
        $(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_strv_t_SOURCES = test/strv.c
test_strv_t_CPPFLAGS = $(test_cppflags)
test_strv_t_LDADD = $(test_ldadd)
test_strv_t_LDFLAGS = $(test_ldflags)

test_sdprocess_t_SOURCES = test/sdprocess.c
test_sdprocess_t_CPPFLAGS = \
	-DTEST_SDPROCESS_DIR=\"$(top_builddir)/src/common/libsdprocess/\" \
	$(test_cppflags)
test_sdprocess_t_LDADD = $(test_ldadd)
test_sdprocess_t_LDFLAGS = $(test_ldflags)

test_echo_SOURCES = test/test_echo.c

test_env_SOURCES = test/test_env.c
