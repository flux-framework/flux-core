AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/live

libfluxdir = $(libdir)/flux
fluxmoddir = $(libfluxdir)/modules
includefluxdir = $(includedir)/flux

#
# Comms module
#
fluxmod_LTLIBRARIES = modctl.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = \
	-module -avoid-version -Wl,--version-script=../module_version.map

modctl_la_SOURCES = modctl.c
modctl_la_LDFLAGS = $(fluxmod_ldflags)
modctl_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
libflux_LTLIBRARIES = \
	libflux-modctl.la

includeflux_HEADERS = \
	modctl.h

libflux_modctl_la_SOURCES = \
	libmodctl.c

libflux_modctl_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

libflux_modctl_la_LDFLAGS = -Wl,--version-script=flux_modctl_version.map

EXTRA_DIST = flux_modctl_version.map
