AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxconnector_LTLIBRARIES = shmem.la

shmem_la_SOURCES = shmem.c

shmem_la_LDFLAGS = -module -Wl,--no-undefined \
	-export-symbols-regex '^connector_init$$' \
	--disable-static -avoid-version -shared -export-dynamic \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

shmem_la_LIBADD = $(JSON_LIBS) $(ZMQ_LIBS)
