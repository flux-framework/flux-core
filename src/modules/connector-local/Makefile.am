AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = connector-local.la

connector_local_la_SOURCES = local.c
connector_local_la_LDFLAGS = $(fluxmod_ldflags) -module
connector_local_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
			    $(top_builddir)/src/common/libflux-core.la \
			    $(JSON_LIBS) $(ZMQ_LIBS)
