AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

lib_LTLIBRARIES = libflux-jsc.la 
fluxcoreinclude_HEADERS = jstatctl.h

libflux_jsc_la_SOURCES = jstatctl.c \
			 jstatctl.h \
			 jstatctl_deprecated.h

libflux_jsc_la_LDFLAGS = $(fluxlib_ldflags) \
			 -export-symbols-regex '^jsc_.*'

libflux_jsc_la_LIBADD = $(top_builddir)/src/modules/kvs/libflux-kvs.la \
			$(top_builddir)/src/common/libflux-internal.la \
			$(top_builddir)/src/common/libflux-core.la
