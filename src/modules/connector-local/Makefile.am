AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

#
# Comms module
#
fluxmod_LTLIBRARIES = connector-local.la

connector_local_la_SOURCES = local.c
connector_local_la_LDFLAGS = $(fluxmod_ldflags) -module
connector_local_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
			    $(top_builddir)/src/common/libflux-core.la \
			    $(ZMQ_LIBS)
