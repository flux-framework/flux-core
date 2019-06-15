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

job_exec_la_SOURCES = \
	job-exec.c

job_exec_la_LDFLAGS = \
	$(fluxmod_ldflags) \
	-module

job_exec_la_LIBADD = \
	$(fluxmod_ldadd) \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS)
