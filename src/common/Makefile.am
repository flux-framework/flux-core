SUBDIRS = libtap \
	  libev \
	  libpmi \
	  liblsd \
	  libutil \
	  libflux \
	  libkvs \
	  libjob \
	  liboptparse \
	  libidset \
	  libtomlc99 \
          libsubprocess \
          libaggregate \
          libschedutil

AM_CFLAGS = $(WARNING_CFLAGS) $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LIBS)

AM_CPPFLAGS =

fluxinclude_HEADERS = core.h
noinst_LTLIBRARIES = libflux-internal.la
libflux_internal_la_SOURCES =
libflux_internal_la_LIBADD = \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libutil/libutil.la \
	$(builddir)/libidset/libidset.la \
	$(builddir)/libev/libev.la \
	$(builddir)/libtomlc99/libtomlc99.la \
	$(JANSSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) $(LIBRT) $(FLUX_SECURITY_LIBS) $(LIBSODIUM_LIBS)
libflux_internal_la_LDFLAGS = $(san_ld_zdef_flag)

lib_LTLIBRARIES = libflux-core.la libflux-optparse.la libflux-idset.la
fluxlib_LTLIBRARIES = libpmi.la libpmi2.la

libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	$(builddir)/libkvs/libkvs.la \
	$(builddir)/libjob/libjob.la \
	$(builddir)/libsubprocess/libsubprocess.la \
	libflux-internal.la
libflux_core_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libflux-core.map \
	-version-info @LIBFLUX_CORE_VERSION_INFO@ \
        -shared -export-dynamic --disable-static \
        $(san_ld_zdef_flag)

libflux_optparse_la_SOURCES =
libflux_optparse_la_LIBADD = \
	$(builddir)/liboptparse/liboptparse.la \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libutil/fsd.lo \
	$(ZMQ_LIBS) $(LIBPTHREAD)
libflux_optparse_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libflux-optparse.map \
	-version-info @LIBFLUX_OPTPARSE_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libflux_idset_la_SOURCES =
libflux_idset_la_LIBADD = \
	$(builddir)/libidset/libidset.la \
	$(builddir)/libutil/veb.lo
libflux_idset_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libflux-idset.map \
	-version-info @LIBFLUX_IDSET_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libpmi_la_SOURCES =
libpmi_la_LIBADD = \
	$(builddir)/libpmi/libpmi.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(LIBDL)
libpmi_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libpmi.map \
	-version-info 0:0:0 \
	-Wl,--defsym=flux_pmi_library=1 \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libpmi2_la_SOURCES =
libpmi2_la_LIBADD = \
	$(builddir)/libpmi/libpmi.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(LIBDL)
libpmi2_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libpmi2.map \
	-version-info 0:0:0 \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)


EXTRA_DIST = \
	libflux-core.map \
	libflux-optparse.map \
	libflux-idset.map \
	libpmi.map \
	libpmi2.map
