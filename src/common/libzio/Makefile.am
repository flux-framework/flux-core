AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxcoreinclude_HEADERS = forkzio.h kz.h zio.h
noinst_LTLIBRARIES = libzio.la
libzio_la_SOURCES = zio.c forkzio.c kz.c
