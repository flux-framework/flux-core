AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/live \
	-I$(top_srcdir)/src/modules/modctl

libfluxdir = $(libdir)/flux
fluxmoddir = $(libfluxdir)/modules
includefluxdir = $(includedir)/flux

#
# Comms module
#
fluxmod_LTLIBRARIES = kvs.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -module -avoid-version -Wl,--version-script=../module.map

kvs_la_SOURCES = kvs.c waitqueue.c
kvs_la_LDFLAGS = $(fluxmod_ldflags)
kvs_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
libflux_LTLIBRARIES = \
	libflux-kvs.la

includeflux_HEADERS = \
	kvs.h

libflux_kvs_la_SOURCES = \
	libkvs.c

libflux_kvs_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

libflux_kvs_la_LDFLAGS = -Wl,--version-script=flux_kvs_version.map
