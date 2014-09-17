AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = api.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^mod_(main|name)$$'

api_la_SOURCES = api.c
api_la_LDFLAGS = $(fluxmod_ldflags)
api_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
fluxcoreinclude_HEADERS = api.h
noinst_LTLIBRARIES = libapi.la
libapi_la_SOURCES = libapi.c
