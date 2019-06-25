AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(VALGRIND_CFLAGS)

noinst_LTLIBRARIES = libshell.la

libshell_la_SOURCES = \
	jobspec.c \
	jobspec.h \
	rcalc.c \
	rcalc.h

fluxlibexec_PROGRAMS = flux-shell

flux_shell_SOURCES = \
	shell.c \
	info.c \
	info.h \
	task.c \
	task.h \
	pmi.c \
	pmi.h \
	io.c \
	io.h

flux_shell_LDADD = \
	$(builddir)/libshell.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-optparse.la

flux_shell_LDFLAGS =

TESTS =

test_ldadd = \
	$(builddir)/libshell.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)


check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh
