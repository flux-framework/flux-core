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
fluxmod_LTLIBRARIES = barrier.la

barrier_la_SOURCES = barrier.c
barrier_la_LDFLAGS = $(fluxmod_ldflags) -module
barrier_la_LIBADD = $(fluxmod_libadd) \
		    $(top_builddir)/src/common/libflux-internal.la \
		    $(top_builddir)/src/common/libflux-core.la \
		    $(ZMQ_LIBS)

#
# API for module
#
fluxcoreinclude_HEADERS = barrier.h
lib_LTLIBRARIES = libflux-barrier.la
libflux_barrier_la_SOURCES = libbarrier.c
libflux_barrier_la_LDFLAGS = $(fluxlib_ldflags) \
			     -export-symbols-regex '^flux_.*'
libflux_barrier_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
			    $(top_builddir)/src/common/libflux-core.la \
			    $(ZMQ_LIBS)
