AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxlib_LTLIBRARIES = libmrpc.la

libmrpc_la_SOURCES = \
	mrpc.c \
	mrpc.h
libmrpc_la_LDFLAGS = -shared -export-dynamic \
		     $(top_builddir)/src/common/libflux-internal.la \
		     -L$(top_builddir)/src/common -lflux-core \
		     -L$(top_builddir)/src/modules/kvs -lkvs \
		     $(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD) $(LIBUTIL) \
		     $(LIBDL) -lrt
