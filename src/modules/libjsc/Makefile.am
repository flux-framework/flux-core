AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxlib_LTLIBRARIES = libjsc.la 
fluxcoreinclude_HEADERS = jstatctl.h

libjsc_la_SOURCES = \
	jstatctl.c \
	jstatctl.h
libjsc_la_LDFLAGS = -shared -export-dynamic --disable-static \
          $(top_builddir)/src/modules/kvs/libkvs.la \
          $(top_builddir)/src/common/libflux-internal.la \
		  $(top_builddir)/src/common/libflux-core.la
