AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include


noinst_LTLIBRARIES = \
	libsubprocess.la

libsubprocess_la_SOURCES = \
	command.c \
	command.h \
	local.c \
	local.h \
	remote.c \
	remote.h \
	server.c \
	server.h \
	util.c \
	util.h \
	subprocess.c \
	subprocess_private.h

fluxcoreinclude_HEADERS = \
	subprocess.h

TESTS = \
	test_cmd.t \
	test_subprocess.t

check_PROGRAMS = \
	$(TESTS) \
	test_echo \
	test_fork_sleep

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/libsubprocess/libsubprocess.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
        $(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_cmd_t_SOURCES = test/cmd.c
test_cmd_t_CPPFLAGS = $(test_cppflags)
test_cmd_t_LDADD = $(test_ldadd)

test_subprocess_t_SOURCES = test/subprocess.c
test_subprocess_t_CPPFLAGS = \
	-DTEST_SUBPROCESS_DIR=\"$(top_builddir)/src/common/libsubprocess/\" \
	$(test_cppflags)
test_subprocess_t_LDADD = $(test_ldadd)

test_echo_SOURCES = test/test_echo.c

test_fork_sleep_SOURCES = test/test_fork_sleep.c
