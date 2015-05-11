AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxhandle_LTLIBRARIES = local.la

fluxhandle_libadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

fluxhandle_ldflags = -avoid-version -module -shared -export-dynamic \
	-export-symbols-regex '^handle_init$$'

local_la_SOURCES = local.c
local_la_LDFLAGS = $(fluxhandle_ldflags)
local_la_LIBADD = $(fluxhandle_libadd)
