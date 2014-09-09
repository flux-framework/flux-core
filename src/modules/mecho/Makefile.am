AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libmrpc

libfluxdir = $(libdir)/flux
fluxmoddir = $(libfluxdir)/modules
includefluxcoredir = $(includedir)/flux/core

#
# Comms module
#
fluxmod_LTLIBRARIES = mecho.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libmrpc/libmrpc.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$'

mecho_la_SOURCES = mecho.c
mecho_la_LDFLAGS = $(fluxmod_ldflags)
mecho_la_LIBADD = $(fluxmod_libadd)
