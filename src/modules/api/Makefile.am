AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = api.la

fluxmod_libadd = \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = --disable-static -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$' \
	-L$(top_builddir)/src/lib/libcore -lflux-core

api_la_SOURCES = api.c
api_la_LDFLAGS = $(fluxmod_ldflags)
api_la_LIBADD = $(fluxmod_libadd)
