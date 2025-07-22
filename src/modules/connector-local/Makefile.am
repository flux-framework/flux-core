AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/librouter \
	-I$(top_builddir)/src/common/libflux \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libconnector-local.la

libconnector_local_la_SOURCES = \
	local.c
