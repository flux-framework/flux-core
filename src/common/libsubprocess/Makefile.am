AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	-DLLOG_ENABLE_DEBUG=1

noinst_LTLIBRARIES = \
	libsubprocess.la

libsubprocess_la_SOURCES = \
	command.c \
	command_private.h \
	local.c \
	local.h \
	fork.c \
	fork.h \
	posix_spawn.c \
	posix_spawn.h \
	remote.c \
	remote.h \
	server.c \
	server.h \
	util.c \
	util.h \
	subprocess.c \
	subprocess_private.h \
	client.h \
	client.c \
	ev_fbuf_read.h \
	ev_fbuf_read.c \
	ev_fbuf_write.h \
	ev_fbuf_write.c \
	fbuf.h \
	fbuf.c \
	fbuf_watcher.h \
	fbuf_watcher.c

fluxcoreinclude_HEADERS = \
	command.h \
	subprocess.h

TESTS = \
	test_command.t \
	test_subprocess.t \
	test_stdio.t \
	test_channel.t \
	test_remote.t \
	test_iostress.t \
	test_iochan.t \
	test_fbuf.t \
	test_fbuf_watcher.t

check_PROGRAMS = \
	$(TESTS) \
	test_echo \
	test_multi_echo \
	test_fork_sleep \
	test_fdcopy

check_LTLIBRARIES = test/libutil.la


TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(builddir)/test/libutil.la \
        $(top_builddir)/src/common/libtestutil/libtestutil.la \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(top_builddir)/src/common/libsubprocess/libsubprocess.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libflux-internal.la

test_ldflags = \
	-no-install

test_cppflags = \
        $(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_libutil_la_SOURCES = \
	test/rcmdsrv.h \
	test/rcmdsrv.c

test_command_t_SOURCES = test/command.c
test_command_t_CPPFLAGS = $(test_cppflags)
test_command_t_LDADD = $(test_ldadd)
test_command_t_LDFLAGS = $(test_ldflags)

test_subprocess_t_SOURCES = test/subprocess.c
test_subprocess_t_CPPFLAGS = \
	-DTEST_SUBPROCESS_DIR=\"$(top_builddir)/src/common/libsubprocess/\" \
	$(test_cppflags)
test_subprocess_t_LDADD = $(test_ldadd)
test_subprocess_t_LDFLAGS = $(test_ldflags)

test_stdio_t_SOURCES = test/stdio.c
test_stdio_t_CPPFLAGS = \
	-DTEST_SUBPROCESS_DIR=\"$(top_builddir)/src/common/libsubprocess/\" \
	$(test_cppflags)
test_stdio_t_LDADD = $(test_ldadd)
test_stdio_t_LDFLAGS = $(test_ldflags)

test_channel_t_SOURCES = test/channel.c
test_channel_t_CPPFLAGS = \
	-DTEST_SUBPROCESS_DIR=\"$(top_builddir)/src/common/libsubprocess/\" \
	$(test_cppflags)
test_channel_t_LDADD = $(test_ldadd)
test_channel_t_LDFLAGS = $(test_ldflags)

test_remote_t_SOURCES = test/remote.c
test_remote_t_CPPFLAGS = \
	-DTEST_SUBPROCESS_DIR=\"$(top_builddir)/src/common/libsubprocess/\" \
	$(test_cppflags)
test_remote_t_LDADD = $(test_ldadd)
test_remote_t_LDFLAGS = $(test_ldflags)

test_iostress_t_SOURCES = test/iostress.c
test_iostress_t_CPPFLAGS = $(test_cppflags)
test_iostress_t_LDADD = $(test_ldadd)
test_iostress_t_LDFLAGS = $(test_ldflags)

test_iochan_t_SOURCES = test/iochan.c
test_iochan_t_CPPFLAGS = \
	-DTEST_SUBPROCESS_DIR=\"$(top_builddir)/src/common/libsubprocess/\" \
	$(test_cppflags)
test_iochan_t_LDADD = $(test_ldadd)
test_iochan_t_LDFLAGS = $(test_ldflags)

test_fbuf_t_SOURCES = test/fbuf.c
test_fbuf_t_CPPFLAGS = $(test_cppflags)
test_fbuf_t_LDADD = $(test_ldadd)
test_fbuf_t_LDFLAGS = $(test_ldflags)

test_fbuf_watcher_t_SOURCES = test/fbuf_watcher.c
test_fbuf_watcher_t_CPPFLAGS = $(test_cppflags)
test_fbuf_watcher_t_LDADD = $(test_ldadd)
test_fbuf_watcher_t_LDFLAGS = $(test_ldflags)

test_echo_SOURCES = test/test_echo.c

test_fdcopy_SOURCES = test/fdcopy.c

test_multi_echo_SOURCES = test/test_multi_echo.c

test_fork_sleep_SOURCES = test/test_fork_sleep.c
