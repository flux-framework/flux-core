SUBDIRS = libtap libev libminilzo libpmi \
	  liblsd libutil libflux libsubprocess libcompat liboptparse

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
	$(builddir)/libminilzo/libminilzo.la \
	$(builddir)/libpmi/libpmi.la \
	$(builddir)/libsubprocess/libsubprocess.la \
	$(builddir)/libcompat/libcompat.la \
	$(LIBMUNGE) $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) $(LIBRT) $(CALIPER_LIBS)
libflux_internal_la_LDFLAGS = $(san_ld_zdef_flag)

lib_LTLIBRARIES = libflux-core.la libflux-optparse.la
fluxlib_LTLIBRARIES = libpmi.la

libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	libflux-internal.la
libflux_core_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libflux-core.map \
        -shared -export-dynamic --disable-static \
        $(san_ld_zdef_flag)

libflux_optparse_la_SOURCES =
libflux_optparse_la_LIBADD = \
	$(builddir)/liboptparse/liboptparse.la \
	$(builddir)/liblsd/liblsd.la \
	$(ZMQ_LIBS) $(LIBPTHREAD)
libflux_optparse_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libflux-optparse.map \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libpmi_la_SOURCES =
libpmi_la_LIBADD = \
	$(builddir)/libpmi/libpmi.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(LIBDL)
libpmi_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libpmi.map \
	-shared -export-dynamic --disable-static \
	-Wl,--no-undefined

EXTRA_DIST = libflux-core.map libflux-optparse.map libpmi.map
