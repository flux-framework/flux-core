AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = \
	libsubprocess.la

libsubprocess_la_SOURCES = \
	subprocess.c \
	subprocess.h

libsubprocess_la_LIBADD = \
	$(top_builddir)/src/modules/libzio/libzio.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JSON_LIBS) $(ZMQ_LIBS) \
	$(LIBPTHREAD) $(LIBDL)

TESTS = \
	test_subprocess.t \
	test_loop.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_subprocess_t_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap
test_subprocess_t_SOURCES = \
	test/subprocess.c
test_subprocess_t_LDADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/modules/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

test_loop_t_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap
test_loop_t_SOURCES = \
	test/loop.c
test_loop_t_LDADD = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/modules/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
