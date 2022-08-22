AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(FLUX_SECURITY_CFLAGS) \
	$(JANSSON_CFLAGS)

fluxmod_LTLIBRARIES = job-ingest.la

job_ingest_la_SOURCES = \
	job-ingest.c \
	workcrew.c \
	workcrew.h \
	worker.c \
	worker.h \
	types.h

job_ingest_la_LDFLAGS = $(fluxmod_ldflags) -module
job_ingest_la_LIBADD = \
		    $(top_builddir)/src/common/libjob/libjob.la \
		    $(top_builddir)/src/common/libflux-internal.la \
		    $(top_builddir)/src/common/libflux-core.la \
		    $(top_builddir)/src/common/libflux-optparse.la \
		    $(JANSSON_LIBS) \
		    $(FLUX_SECURITY_LIBS)

fluxschemadir = $(datadir)/flux/schema/jobspec/
dist_fluxschema_DATA = \
	schemas/jobspec.jsonschema \
	schemas/jobspec_v1.jsonschema
