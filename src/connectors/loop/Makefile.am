AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxconnector_LTLIBRARIES = loop.la

loop_la_SOURCES = loop.c

loop_la_LDFLAGS = -module -Wl,--no-undefined \
	-export-symbols-regex '^connector_init$$' \
	--disable-static -avoid-version -shared -export-dynamic \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

loop_la_LIBADD = $(JSON_LIBS) $(ZMQ_LIBS)
