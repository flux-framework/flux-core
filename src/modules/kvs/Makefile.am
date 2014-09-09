AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux

libfluxdir = $(libdir)/flux
fluxmoddir = $(libfluxdir)/modules
includefluxcoredir = $(includedir)/flux/core

#
# Comms module
#
fluxmod_LTLIBRARIES = kvs.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
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
noinst_LTLIBRARIES = libkvs.la
includefluxcore_HEADERS = kvs.h
libkvs_la_SOURCES = libkvs.c
