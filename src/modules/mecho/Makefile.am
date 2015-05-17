AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = mecho.la

fluxmod_libadd = \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$' \
	-L$(top_builddir)/src/lib/libcore -lflux-core \
	-L$(top_builddir)/src/modules/kvs -lkvs

mecho_la_SOURCES = mecho.c
mecho_la_LDFLAGS = $(fluxmod_ldflags)
mecho_la_LIBADD = $(fluxmod_libadd)
