AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

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
