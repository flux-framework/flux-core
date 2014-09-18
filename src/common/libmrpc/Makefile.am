AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libmrpc.la

libmrpc_la_SOURCES = \
	mrpc.c \
	mrpc.h
