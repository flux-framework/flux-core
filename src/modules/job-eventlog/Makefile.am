AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(FLUX_SECURITY_CFLAGS) $(YAMLCPP_CFLAGS)

fluxmod_LTLIBRARIES = job-eventlog.la

job_eventlog_la_SOURCES = \
	job-eventlog.c


job_eventlog_la_LDFLAGS = $(fluxmod_ldflags) -module
job_eventlog_la_LIBADD = $(fluxmod_libadd) \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(ZMQ_LIBS)
