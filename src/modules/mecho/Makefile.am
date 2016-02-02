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
fluxmod_LTLIBRARIES = mecho.la

mecho_la_SOURCES = mecho.c
mecho_la_LDFLAGS = $(fluxmod_ldflags) -module
mecho_la_LIBADD = $(top_builddir)/src/modules/libmrpc/libflux-mrpc.la \
		  $(top_builddir)/src/common/libflux-core.la \
		  $(top_builddir)/src/common/libflux-internal.la
