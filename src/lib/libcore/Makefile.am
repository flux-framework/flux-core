AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = 

fluxinclude_HEADERS = core.h
fluxlib_LTLIBRARIES = libflux-core.la
libflux_core_la_SOURCES = 
libflux_core_la_LIBADD = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/live/liblive.la \
	$(top_builddir)/src/modules/barrier/libbarrier.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL)

libflux_core_la_LDFLAGS = -Wl,--version-script=$(srcdir)/version.map

EXTRA_DIST = version.map
