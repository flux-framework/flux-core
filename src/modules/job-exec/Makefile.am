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
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = \
	libjob-exec.la

libjob_exec_la_SOURCES = \
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

test_ldadd = \
	$(builddir)/libjob-exec.la \
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
	test/rset.c
test_rset_t_CPPFLAGS = \
	$(test_cppflags)
test_rset_t_LDADD = \
	$(test_ldadd)
test_rset_t_LDFLAGS = \
	$(test_ldflags)
