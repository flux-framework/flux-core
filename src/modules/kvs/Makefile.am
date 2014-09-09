AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
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
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
        -export-symbols-regex '(^mod)_(main|name)$$'

kvs_la_SOURCES = \
	kvs.c \
	waitqueue.c \
	waitqueue.h

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
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

libflux_kvs_la_LDFLAGS = -Wl,--version-script=$(srcdir)/flux_kvs_version.map

EXTRA_DIST = flux_kvs_version.map
