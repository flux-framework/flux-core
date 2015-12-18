SUBDIRS = libtap libev libsophia libpmi-client libpmi-server \
	  liblsd libutil libflux libsubprocess libcompat

AM_CFLAGS = @GCCWARN@ $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS =

fluxinclude_HEADERS = core.h
noinst_LTLIBRARIES = libflux-internal.la
libflux_internal_la_SOURCES =
libflux_internal_la_LIBADD = \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libutil/libutil.la \
	$(builddir)/libev/libev.la \
	$(builddir)/libsophia/libsophia.la \
	$(builddir)/libpmi-client/libpmi-client.la \
	$(builddir)/libpmi-server/libpmi-server.la \
	$(builddir)/libsubprocess/libsubprocess.la \
	$(builddir)/libcompat/libcompat.la \
	$(LIBMUNGE) $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) -lrt
libflux_internal_la_LDFLAGS = -Wl,--no-undefined

fluxlib_LTLIBRARIES = libflux-core.la
libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	libflux-internal.la
#-lrt is for clock_gettime, this should be abstracted

libflux_core_la_LDFLAGS = -Wl,--version-script=$(srcdir)/version.map \
			  -shared -export-dynamic --disable-static \
			  -Wl,--no-undefined

EXTRA_DIST = version.map
