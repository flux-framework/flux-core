AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = mecho.la

fluxmod_libadd = \
	$(JSON_LIBS) $(ZMQ_LIBS) \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/modules/kvs/libkvs.la

general_ldflags = --disable-static -avoid-version -shared -export-dynamic \
	$(LIBMUNGE) $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL)

fluxmod_ldflags = -module \
	-export-symbols-regex '^mod_(main|name)$$' \
	$(general_ldflags)

mecho_la_SOURCES = mecho.c
mecho_la_LDFLAGS = $(fluxmod_ldflags)
mecho_la_LIBADD = $(fluxmod_libadd)
