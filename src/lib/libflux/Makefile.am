AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/modules/live \
	-I$(top_srcdir)/src/modules/modctl

libfluxdir=$(libdir)/flux
includefluxdir=$(includedir)/flux

includeflux_HEADERS = \
	flux.h \
	handle.h \
	reactor.h \
	reduce.h \
	security.h

libflux_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	log.c \
	brokerops.c \
	handle.c \
	reduce.c \
	security.c

libflux_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la

libflux_la_LDFLAGS = -Wl,--version-script=flux_version.map
