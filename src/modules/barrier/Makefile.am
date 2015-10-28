AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = barrier.la

barrier_la_SOURCES = barrier.c
barrier_la_LDFLAGS = $(fluxmod_ldflags) -module
barrier_la_LIBADD = $(fluxmod_libadd) \
		    $(top_builddir)/src/common/libflux-internal.la \
		    $(top_builddir)/src/common/libflux-core.la \
		    $(JSON_LIBS) $(ZMQ_LIBS)

#
# API for module
#
fluxcoreinclude_HEADERS = barrier.h
fluxlib_LTLIBRARIES = libbarrier.la
libbarrier_la_SOURCES = libbarrier.c
libbarrier_la_LDFLAGS = $(fluxlib_ldflags) \
			-export-symbols-regex '^flux_.*'
libbarrier_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
		       $(top_builddir)/src/common/libflux-core.la \
		       $(JSON_LIBS) $(ZMQ_LIBS)
