AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/libutil

libfluxdir=$(libdir)/flux

libflux_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	log.c \
	broker.c \
	handle.c \
	reduce.c \
	security.c

libflux_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la

libflux_la_LDFLAGS = -Wl,--version-script=flux_version.map
