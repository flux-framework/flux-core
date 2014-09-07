AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I../../common/libutil

noinst_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	log.c \
	api.c \
	broker.c \
	handle.c \
	reduce.c \
	security.c

libflux_la_LIBADD = \
	$(top_builddir)/src/common/libutil/libutil.la
