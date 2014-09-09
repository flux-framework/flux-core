AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/modules/kvs

includefluxcoredir=$(includedir)/flux/core

noinst_LTLIBRARIES = libmrpc.la
includefluxcore_HEADERS = mrpc.h
libmrpc_la_SOURCES = mrpc.c
