SUBDIRS = libtap libev liblsd libutil libflux libzio

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS =

fluxinclude_HEADERS = core.h
fluxlib_LTLIBRARIES = libflux-core.la
libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	$(builddir)/libev/libev.la \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libzio/libzio.la \
	$(builddir)/libutil/libutil.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL)

libflux_core_la_LDFLAGS = -Wl,--version-script=$(srcdir)/version.map \
			  -shared -export-dynamic --disable-static

EXTRA_DIST = version.map
