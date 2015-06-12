AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libcompat.la

libcompat_la_SOURCES = \
	handle.c \
	handle.h \
	reactor.c \
	reactor.h \
	compat.h
