AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libkz.la

libkz_la_SOURCES = \
	kz.c \
	kz.h
libkz_la_LDFLAGS =
libkz_la_LIBADD = $(top_builddir)/src/modules/libzio/libzio.la \
		  $(top_builddir)/src/common/libflux-core.la
