AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = 

libfluxdir=$(libdir)/flux
includefluxdir=$(includedir)/flux
ldconfdir=/etc/ld.so.conf.d

includeflux_HEADERS = core.h
libflux_LTLIBRARIES = libflux-core.la
libflux_core_la_SOURCES = 
libflux_core_la_LIBADD = \
	$(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/api/libapi.la \
	$(top_builddir)/src/modules/live/liblive.la \
	$(top_builddir)/src/modules/modctl/libmodctl.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD)

libflux_core_la_LDFLAGS = -Wl,--version-script=$(srcdir)/version.map

if WITH_PKG_CONFIG
pkgconfig_DATA = flux-core.pc
endif

ldconf_DATA = flux-libs.conf

EXTRA_DIST = version.map
