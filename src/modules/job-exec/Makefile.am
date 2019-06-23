AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(JANSSON_CFLAGS)

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
	rset.c \
	rset.h \
	testexec.c \
	exec.c

job_exec_la_LDFLAGS = \
	$(fluxmod_ldflags) \
	-module

job_exec_la_LIBADD = \
	$(fluxmod_ldadd) \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	libbulk-exec.la \
	$(ZMQ_LIBS)

bulk_exec_SOURCES = \
	test/bulk-exec.c

bulk_exec_LDADD = \
	$(fluxmod_ldadd) \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-idset.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	libbulk-exec.la \
	$(ZMQ_LIBS)

test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(JANSSON_LIBS)

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
