AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libcompat.la

libcompat_la_SOURCES = \
	handle.c \
	handle.h \
	reactor.c \
	reactor.h \
	info.c \
	info.h \
	request.c \
	request.h \
	rpc.c \
	rpc.h \
	compat.h
