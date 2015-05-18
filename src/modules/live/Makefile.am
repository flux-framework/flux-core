AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = live.la

fluxmod_libadd = \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

general_ldflags = --disable-static -avoid-version -shared -export-dynamic \
		  $(top_builddir)/src/common/libflux-internal.la \
		  -L$(top_builddir)/src/common -lflux-core \
		  -L$(top_builddir)/src/modules/kvs -lkvs
fluxmod_ldflags = -module \
        -export-symbols-regex '^mod_(main|name)$$' \
	$(general_ldflags)

live_la_SOURCES = live.c
live_la_LDFLAGS = $(fluxmod_ldflags)
live_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
fluxcoreinclude_HEADERS = live.h
fluxlib_LTLIBRARIES = liblive.la
liblive_la_SOURCES = liblive.c
liblive_la_LDFLAGS = $(general_ldflags) -Wl,--no-undefined
