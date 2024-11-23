SUBDIRS = \
	libtap \
	libtestutil \
	libev \
	libyuarel \
	libpmi \
	liblsd \
	libutil \
	libflux \
	libfluxutil \
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
	libtaskmap \
	libfilemap \
	libsdexec \
	libmissing

AM_CFLAGS = $(WARNING_CFLAGS) $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = $(CODE_COVERAGE_CPPFLAGS)

fluxinclude_HEADERS = core.h schedutil.h
noinst_LTLIBRARIES = libflux-internal.la
libflux_internal_la_SOURCES =
libflux_internal_la_LIBADD = \
	$(builddir)/libfluxutil/libfluxutil.la \
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
	$(builddir)/libmissing/libmissing.la \
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

fluxlib_LTLIBRARIES = \
	flux/libpmi.la \
	flux/libpmi2.la

#  With make -j, libpmi*.so will be installed in parallel with dependencies
#  (libflux-taskmap.so and libflux-idset.so), so one or both .la files may
#  not be created by the time they are needed for the libpmi*.so relink,
#  resulting in an error like
#
#    /usr/bin/ld: cannot find -lflux-taskmap
#
#  This workaround forces the install of fluxlib_LTLIBRARIES after all
#  lib_LTLIBRARIES.
#
#  ref: https://debbugs.gnu.org/cgi/bugreport.cgi?bug=7328
#
install_fluxlibLTLIBRARIES = install-fluxlibLTLIBRARIES
$(install_fluxlibLTLIBRARIES): install-libLTLIBRARIES

libflux_core_la_SOURCES =
libflux_core_la_LIBADD = \
	$(builddir)/libflux/libflux.la \
	$(builddir)/libkvs/libkvs.la \
	$(builddir)/libjob/libjob.la \
	$(builddir)/libsubprocess/libsubprocess.la \
	libflux-internal.la
libflux_core_la_LDFLAGS = \
	-export-symbols-regex "^(flux_|__asan)" \
	-version-info @LIBFLUX_CORE_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
	$(san_ld_zdef_flag)

libflux_optparse_la_SOURCES =
libflux_optparse_la_LIBADD = \
	$(builddir)/liboptparse/liboptparse.la \
	$(builddir)/libczmqcontainers/libczmqcontainers.la \
	$(builddir)/liblsd/liblsd.la \
	$(builddir)/libutil/fsd.lo \
	$(builddir)/libutil/parse_size.lo \
	$(builddir)/libmissing/libmissing.la \
	$(LIBPTHREAD)
libflux_optparse_la_LDFLAGS = \
	-export-symbols-regex "^(optparse_|__asan)" \
	-version-info @LIBFLUX_OPTPARSE_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
	$(san_ld_zdef_flag)

libflux_idset_la_SOURCES =
libflux_idset_la_LIBADD = \
	$(builddir)/libidset/libidset.la
libflux_idset_la_LDFLAGS = \
	-export-symbols-regex "^(idset_|__asan)" \
	-version-info @LIBFLUX_IDSET_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
	$(san_ld_zdef_flag)

libflux_schedutil_la_SOURCES =
libflux_schedutil_la_LIBADD = \
	$(builddir)/libschedutil/libschedutil.la \
	$(builddir)/libczmqcontainers/libczmqcontainers.la \
	libflux-core.la \
	$(JANSSON_LIBS)
libflux_schedutil_la_LDFLAGS = \
	-export-symbols-regex "^(schedutil_|__asan)" \
	-version-info @LIBFLUX_SCHEDUTIL_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
	$(san_ld_zdef_flag)

libflux_hostlist_la_SOURCES =
libflux_hostlist_la_LIBADD = \
	$(builddir)/libhostlist/libhostlist.la
libflux_hostlist_la_LDFLAGS = \
	-export-symbols-regex "^(hostlist_|__asan)" \
	-version-info @LIBFLUX_HOSTLIST_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
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
	-export-symbols-regex "^(taskmap_|__asan)" \
	-version-info @LIBFLUX_TASKMAP_VERSION_INFO@ \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
	$(san_ld_zdef_flag)

flux_libpmi_la_SOURCES =
flux_libpmi_la_LIBADD = \
	libflux-taskmap.la \
	libflux-idset.la \
	$(builddir)/libpmi/libpmi_client.la \
	$(builddir)/libpmi/libpmi_common.la \
	$(builddir)/libutil/aux.lo \
	$(builddir)/libmissing/libmissing.la

flux_libpmi_la_LDFLAGS = \
	-export-symbols-regex "^(PMI_|flux_pmi_library|__asan)" \
	-version-info 0:0:0 \
	-Wl,--defsym=flux_pmi_library=1 \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
	$(san_ld_zdef_flag)

flux_libpmi2_la_SOURCES =
flux_libpmi2_la_LIBADD = \
	libflux-taskmap.la \
	libflux-idset.la \
	$(builddir)/libpmi/libpmi_client.la \
	$(builddir)/libpmi/libpmi_common.la \
	$(builddir)/libutil/aux.lo \
	$(builddir)/libmissing/libmissing.la
flux_libpmi2_la_LDFLAGS = \
	-export-symbols-regex "^(PMI2_|flux_pmi_library|__asan)" \
	-version-info 0:0:0 \
	-Wl,--defsym=flux_pmi_library=1 \
	-shared -export-dynamic --disable-static \
	$(ld_gc_sections) \
	$(san_ld_zdef_flag)
