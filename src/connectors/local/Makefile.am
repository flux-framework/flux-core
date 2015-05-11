AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxconnector_LTLIBRARIES = local.la

fluxconnector_libadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxconnector_ldflags = -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^connector_init$$'

local_la_SOURCES = local.c
local_la_LDFLAGS = $(fluxconnector_ldflags)
local_la_LIBADD = $(fluxconnector_libadd)
