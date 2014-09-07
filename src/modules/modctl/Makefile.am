AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux \
	-I$(top_srcdir)/src/modules/kvs

#
# Comms module
#
fluxmoddir = $(libdir)/flux-modules
fluxmod_LTLIBRARIES = modctl.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -module -avoid-version -Wl,--version-script=../module.map

modctl_la_SOURCES = modctl.c
modctl_la_LDFLAGS = $(fluxmod_ldflags)
modctl_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
lib_LTLIBRARIES = \
	libflux-modctl.la

libflux_modctl_la_SOURCES = \
	libmodctl.c

libflux_modctl_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

libflux_modctl_la_LDFLAGS = -Wl,--version-script=flux_modctl_version.map
