SUBDIRS = \
	libtap \
	libtestutil \
	libev \
	libyuarel \
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
	libschedutil \
	libeventlog \
	libioencode \
	librouter \
	libdebugged \
	libterminus \
	libcontent \
	libhostlist \
	librlist \
	libczmqcontainers \
	libccan \
	libzmqutil \
	libtaskmap

if HAVE_LIBSYSTEMD
SUBDIRS += libsdprocess
endif

AM_CFLAGS = $(WARNING_CFLAGS) $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LIBS)

AM_CPPFLAGS =

fluxinclude_HEADERS = core.h schedutil.h
noinst_LTLIBRARIES = libflux-internal.la
libflux_internal_la_SOURCES =
libflux_internal_la_LIBADD = \
	$(builddir)/libflux/libmessage.la \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libccan/libccan.la \
	$(builddir)/libutil/libutil.la \
	$(builddir)/libidset/libidset.la \
	$(builddir)/libev/libev.la \
	$(builddir)/libyuarel/libyuarel.la \
	$(builddir)/libtomlc99/libtomlc99.la \
	$(builddir)/libeventlog/libeventlog.la \
	$(builddir)/libioencode/libioencode.la \
	$(builddir)/librouter/librouter.la \
	$(builddir)/libhostlist/libhostlist.la \
	$(builddir)/libczmqcontainers/libczmqcontainers.la \
	$(builddir)/libcontent/libcontent.la \
	$(builddir)/libtaskmap/libtaskmap.la \
	$(JANSSON_LIBS) \
	$(LIBUUID_LIBS) \
	$(LIBPTHREAD) \
	$(LIBDL) \
	$(LIBRT) \
	$(FLUX_SECURITY_LIBS)
libflux_internal_la_LDFLAGS = $(san_ld_zdef_flag)

lib_LTLIBRARIES = libflux-core.la \
	libflux-optparse.la \
	libflux-idset.la \
	libflux-schedutil.la \
	libflux-hostlist.la \
	libflux-taskmap.la

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
	$(builddir)/libczmqcontainers/libczmqcontainers.la \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libutil/fsd.lo \
	$(LIBPTHREAD)
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

libflux_schedutil_la_SOURCES =
libflux_schedutil_la_LIBADD = \
	$(builddir)/libschedutil/libschedutil.la \
	$(builddir)/libczmqcontainers/libczmqcontainers.la \
	libflux-core.la \
	$(JANSSON_LIBS)
libflux_schedutil_la_LDFLAGS = \
	-Wl,--version-script=$(srcdir)/libflux-schedutil.map \
	-version-info @LIBFLUX_SCHEDUTIL_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libflux_hostlist_la_SOURCES =
libflux_hostlist_la_LIBADD = \
	$(builddir)/libhostlist/libhostlist.la
libflux_hostlist_la_LDFLAGS = \
	-Wl,--version-script=$(srcdir)/libflux-hostlist.map \
	-version-info @LIBFLUX_HOSTLIST_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libflux_taskmap_la_SOURCES =
libflux_taskmap_la_LIBADD = \
	$(builddir)/libtaskmap/libtaskmap.la \
	$(builddir)/libczmqcontainers/libczmqcontainers.la  \
	$(builddir)/libutil/libutil.la  \
	$(builddir)/libccan/libccan.la  \
	$(builddir)/libtomlc99/libtomlc99.la  \
	$(builddir)/libyuarel/libyuarel.la  \
	libflux-core.la \
	libflux-idset.la \
	$(JANSSON_LIBS)
libflux_taskmap_la_LDFLAGS = \
	-Wl,--version-script=$(srcdir)/libflux-taskmap.map \
	-version-info @LIBFLUX_TASKMAP_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libpmi_la_SOURCES =
libpmi_la_LIBADD = \
	$(builddir)/libpmi/libpmi_client.la \
	$(builddir)/libutil/aux.lo
libpmi_la_LDFLAGS = \
	-Wl,--version-script=$(srcdir)/libpmi.map \
	-version-info 0:0:0 \
	-Wl,--defsym=flux_pmi_library=1 \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

libpmi2_la_SOURCES =
libpmi2_la_LIBADD = \
	$(builddir)/libpmi/libpmi_client.la \
	$(builddir)/libutil/aux.lo
libpmi2_la_LDFLAGS = \
	-Wl,--version-script=$(srcdir)/libpmi2.map \
	-version-info 0:0:0 \
	-Wl,--defsym=flux_pmi_library=1 \
	-shared -export-dynamic --disable-static \
	$(san_ld_zdef_flag)

EXTRA_DIST = \
	libflux-core.map \
	libflux-optparse.map \
	libflux-idset.map \
	libflux-schedutil.map \
	libflux-hostlist.map \
	libflux-taskmap.map \
	libpmi.map \
	libpmi2.map
