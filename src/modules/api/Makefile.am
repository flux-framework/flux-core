AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = api.la

fluxmod_libadd = \
	$(JSON_LIBS) $(ZMQ_LIBS)

fluxmod_ldflags = --disable-static -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$' \
	$(top_builddir)/src/common/libflux-core.la

api_la_SOURCES = api.c
api_la_LDFLAGS = $(fluxmod_ldflags)
api_la_LIBADD = $(fluxmod_libadd)
