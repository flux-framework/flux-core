AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux

fluxconnector_LTLIBRARIES = rlocal.la

rlocal_la_SOURCES = rlocal.c

rlocal_la_LDFLAGS = -module $(san_ld_zdef_flag) \
	-export-symbols-regex '^connector_init$$' \
	--disable-static -avoid-version -shared -export-dynamic

rlocal_la_LIBADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la
