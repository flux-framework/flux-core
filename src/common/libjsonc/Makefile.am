AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libjsonc.la

libjsonc_la_SOURCES = \
	request.c \
	request.h \
	rpc.c \
	rpc.h \
	jsonc.h
