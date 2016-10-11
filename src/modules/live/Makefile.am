AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

#
# Comms module
#
fluxmod_LTLIBRARIES = live.la

live_la_SOURCES = live.c
live_la_LDFLAGS = $(fluxmod_ldflags) -module
live_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
		 $(top_builddir)/src/common/libflux-core.la \
                 $(top_builddir)/src/common/libflux-optparse.la \
		 $(top_builddir)/src/modules/kvs/libflux-kvs.la \
		 $(top_builddir)/src/modules/barrier/libflux-barrier.la \
		 $(ZMQ_LIBS)

#
# API for module
#
fluxcoreinclude_HEADERS = live.h

lib_LTLIBRARIES = libflux-live.la

libflux_live_la_SOURCES = liblive.c

libflux_live_la_LDFLAGS = $(fluxlib_ldflags) \
			  -export-symbols-regex '^flux_.*'

libflux_live_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
			 $(top_builddir)/src/common/libflux-core.la \
			 $(top_builddir)/src/modules/kvs/libflux-kvs.la \
			 $(ZMQ_LIBS)
