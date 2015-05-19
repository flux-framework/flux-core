SUBDIRS = libtap libev liblsd libutil libflux libzio

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS =

fluxinclude_HEADERS = core.h
fluxlib_LTLIBRARIES = libflux-core.la libflux-internal.la
libflux_internal_la_SOURCES =
libflux_internal_la_LIBADD = \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libutil/libutil.la \
	$(builddir)/libev/libev.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) -lrt

libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	libflux-internal.la
#-lrt is for clock_gettime, this should be abstracted

libflux_core_la_LDFLAGS = -Wl,--version-script=$(srcdir)/version.map \
			  -shared -export-dynamic --disable-static \
			  -Wl,--no-undefined

EXTRA_DIST = version.map
