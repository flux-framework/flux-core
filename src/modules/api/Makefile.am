AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux

#
# Comms module
#
fluxmoddir = $(libdir)/flux-modules
fluxmod_LTLIBRARIES = api.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -module -avoid-version -Wl,--version-script=../module.map

api_la_SOURCES = api.c
api_la_LDFLAGS = $(fluxmod_ldflags)
api_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
lib_LTLIBRARIES = \
	libflux-api.la

libflux_api_la_SOURCES = \
	libapi.c

libflux_api_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

libflux_api_la_LDFLAGS = -Wl,--version-script=flux_api_version.map
