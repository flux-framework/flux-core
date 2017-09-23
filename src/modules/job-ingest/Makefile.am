# AM_CFLAGS = \
# 	@GCCWARN@ \
# 	$(CODE_COVERAGE_CFLAGS)

# AM_LDFLAGS = \
# 	$(CODE_COVERAGE_LDFLAGS)

if ENABLE_JOBSPEC
AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

fluxmod_LTLIBRARIES = job-ingest.la

job_ingest_la_SOURCES = job-ingest.cpp
job_ingest_la_LDFLAGS = $(fluxmod_ldflags) -module
job_ingest_la_LIBADD = $(fluxmod_libadd) \
		    $(top_builddir)/src/common/libflux-internal.la \
		    $(top_builddir)/src/common/libflux-core.la \
                    $(top_builddir)/src/common/libjobspec/libjobspec.la \
                    $(ZMQ_LIBS) $(YAMLCPP_LIBS)
endif
