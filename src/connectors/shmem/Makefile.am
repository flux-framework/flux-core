AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

fluxconnector_LTLIBRARIES = shmem.la

shmem_la_SOURCES = shmem.c

shmem_la_LDFLAGS = -module $(san_ld_zdef_flag) \
	-export-symbols-regex '^connector_init$$' \
	--disable-static -avoid-version -shared -export-dynamic \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la

shmem_la_LIBADD = $(ZMQ_LIBS)
