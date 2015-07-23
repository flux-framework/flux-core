AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = connector-local.la

fluxmod_libadd = \
	$(JSON_LIBS) $(ZMQ_LIBS)

fluxmod_ldflags = --disable-static -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$'

connector_local_la_SOURCES = local.c
connector_local_la_LDFLAGS = $(fluxmod_ldflags)
connector_local_la_LIBADD = $(fluxmod_libadd) \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
