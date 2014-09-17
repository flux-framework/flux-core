AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)

#
# Comms module
#
fluxmod_LTLIBRARIES = modctl.la

fluxmod_libadd = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
        -export-symbols-regex '^mod_(main|name)$$'

modctl_la_SOURCES = modctl.c
modctl_la_LDFLAGS = $(fluxmod_ldflags)
modctl_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
fluxcoreinclude_HEADERS = modctl.h
noinst_LTLIBRARIES = libmodctl.la
libmodctl_la_SOURCES = libmodctl.c
