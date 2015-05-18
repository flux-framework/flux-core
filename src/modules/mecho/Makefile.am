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
general_ldflags = --disable-static -avoid-version -shared -export-dynamic \
	-L$(top_builddir)/src/lib/libcore -lflux-core \
	-L$(top_builddir)/src/modules/kvs -lkvs
fluxmod_ldflags = -module \
	-export-symbols-regex '^mod_(main|name)$$' \
	$(general_ldflags)

mecho_la_SOURCES = mecho.c
mecho_la_LDFLAGS = $(fluxmod_ldflags)
mecho_la_LIBADD = $(fluxmod_libadd)
