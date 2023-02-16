AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_builddir)/src/common/libccan \
	$(JANSSON_CFLAGS)

fluxmod_LTLIBRARIES = \
	job-exec.la

noinst_LTLIBRARIES = \
	libbulk-exec.la

noinst_PROGRAMS = \
	bulk-exec

libbulk_exec_la_SOURCES = \
	bulk-exec.h \
	bulk-exec.c

job_exec_la_SOURCES = \
	job-exec.h \
	job-exec.c \
	checkpoint.h \
	checkpoint.c \
	exec_config.h \
	exec_config.c \
	rset.c \
	rset.h \
	testexec.c \
	exec.c

if HAVE_LIBSYSTEMD
job_exec_la_SOURCES += sdexec.c
endif

job_exec_la_LDFLAGS = \
	$(fluxmod_ldflags) \
	-module

job_exec_la_LIBADD = \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(JANSSON_LIBS) \
	libbulk-exec.la

if HAVE_LIBSYSTEMD
job_exec_la_LIBADD += $(top_builddir)/src/common/libsdprocess/libsdprocess.la
endif

bulk_exec_SOURCES = \
	test/bulk-exec.c

bulk_exec_LDADD = \
	libbulk-exec.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-idset.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(top_builddir)/src/common/libutil/libutil.la

test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBPTHREAD) $(JANSSON_LIBS)

test_ldflags = \
	-no-install

test_cppflags = \
	$(AM_CPPFLAGS)

TESTS = \
	test_rset.t

check_PROGRAMS = \
	$(TESTS)

test_rset_t_SOURCES = \
	rset.c \
	rset.h \
	test/rset.c
test_rset_t_CPPFLAGS = \
	$(test_cppflags)
test_rset_t_LDADD = \
	$(test_ldadd)
test_rset_t_LDFLAGS = \
	$(test_ldflags)
