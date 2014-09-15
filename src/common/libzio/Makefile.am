AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/modules/kvs

fluxcoreinclude_HEADERS = forkzio.h kz.h zio.h
noinst_LTLIBRARIES = libzio.la
libzio_la_SOURCES = zio.c forkzio.c kz.c
