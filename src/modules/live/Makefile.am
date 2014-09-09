AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/modules/kvs

libfluxdir = $(libdir)/flux
fluxmoddir = $(libfluxdir)/modules
includefluxcoredir = $(includedir)/flux/core

#
# Comms module
#
fluxmod_LTLIBRARIES = live.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
        -export-symbols-regex '(^mod)_(main|name)$$'

live_la_SOURCES = live.c
live_la_LDFLAGS = $(fluxmod_ldflags)
live_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
includefluxcore_HEADERS = live.h
noinst_LTLIBRARIES = liblive.la
liblive_la_SOURCES = liblive.c
