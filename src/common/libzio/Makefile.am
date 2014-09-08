AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/modules/live \
	-I$(top_srcdir)/src/modules/modctl

noinst_LTLIBRARIES = libzio.la

libzio_la_SOURCES = \
	zio.c \
	forkzio.c \
	kz.c
