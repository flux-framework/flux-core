SUBDIRS = libtap \
	  libev \
	  libminilzo \
	  libjson-c \
	  libpmi \
	  liblsd \
	  libutil \
	  libflux \
	  libkvs \
	  libkz \
	  libjsc \
	  libsubprocess \
	  libcompat \
	  liboptparse

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
	$(builddir)/libkz/libkz.la \
	$(builddir)/libsubprocess/libsubprocess.la \
	$(builddir)/libcompat/libcompat.la \
	$(builddir)/libjson-c/libjson-c.la \
	$(LIBMUNGE) $(JANSSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) $(LIBRT)
libflux_internal_la_LDFLAGS = $(san_ld_zdef_flag)

lib_LTLIBRARIES = libflux-core.la libflux-optparse.la
fluxlib_LTLIBRARIES = libpmi.la libpmi2.la

libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	$(builddir)/libkvs/libkvs.la \
	$(builddir)/libjsc/libjsc.la \
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
	-Wl,--defsym=flux_pmi_library=1 \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libpmi2_la_SOURCES =
libpmi2_la_LIBADD = \
	$(builddir)/libpmi/libpmi.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(LIBDL)
libpmi2_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libpmi2.map \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)


EXTRA_DIST = libflux-core.map libflux-optparse.map libpmi.map libpmi2.map
