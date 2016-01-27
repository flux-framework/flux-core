AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxlib_LTLIBRARIES = libjsc.la 
fluxcoreinclude_HEADERS = jstatctl.h

libjsc_la_SOURCES = \
	jstatctl.c \
	jstatctl.h \
	jstatctl_deprecated.h

libjsc_la_LDFLAGS = $(fluxlib_ldflags) \
		    -export-symbols-regex '^jsc_.*'

libjsc_la_LIBADD = $(top_builddir)/src/modules/kvs/libkvs.la \
		   $(top_builddir)/src/common/libflux-internal.la \
		   $(top_builddir)/src/common/libflux-core.la
