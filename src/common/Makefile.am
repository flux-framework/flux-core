SUBDIRS = libtap \
	  libev \
	  libminilzo \
	  libpmi \
	  liblsd \
	  libutil \
	  libflux \
	  libkvs \
	  libjsc \
	  libjob \
	  libsubprocess \
	  libcompat \
	  liboptparse \
	  libidset \
	  libtomlc99 \
	  libkz

if ENABLE_JOBSPEC
SUBDIRS += libjobspec
endif

AM_CFLAGS = $(WARNING_CFLAGS) $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LIBS)

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
	$(builddir)/libtomlc99/libtomlc99.la \
	$(LIBMUNGE) $(JANSSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) \
	$(LIBDL) $(LIBRT)
libflux_internal_la_LDFLAGS = $(san_ld_zdef_flag)

lib_LTLIBRARIES = libflux-core.la libflux-optparse.la libflux-idset.la
if ENABLE_JOBSPEC
lib_LTLIBRARIES += libflux-jobspec.la
endif
fluxlib_LTLIBRARIES = libpmi.la libpmi2.la

libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	$(builddir)/libkvs/libkvs.la \
	$(builddir)/libjsc/libjsc.la \
	$(builddir)/libjob/libjob.la \
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

libflux_idset_la_SOURCES =
libflux_idset_la_LIBADD = \
	$(builddir)/libidset/libidset.la \
	$(builddir)/libutil/veb.lo
libflux_idset_la_LDFLAGS = \
        -Wl,--version-script=$(srcdir)/libflux-idset.map \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

if ENABLE_JOBSPEC
libflux_jobspec_la_SOURCES =
libflux_jobspec_la_LIBADD = $(builddir)/libjobspec/libjobspec.la
endif

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


EXTRA_DIST = \
	libflux-core.map \
	libflux-optparse.map \
	libflux-idset.map \
	libpmi.map \
	libpmi2.map
