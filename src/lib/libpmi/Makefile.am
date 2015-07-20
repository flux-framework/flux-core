AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
        -I$(top_srcdir) -I$(top_srcdir)/src/include


fluxinclude_HEADERS = pmi.h
fluxlib_LTLIBRARIES = libpmi.la

libpmi_la_SOURCES = \
	pmi.c

libpmi_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/modules/kvs/libkvs.la 

libpmi_la_LDFLAGS = -Wl,--version-script=$(srcdir)/version.map

EXTRA_DIST = version.map
