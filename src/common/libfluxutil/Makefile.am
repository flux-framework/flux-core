AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = libfluxutil.la
libfluxutil_la_SOURCES = \
	method.h \
	method.c \
	policy.h \
	policy.c

libfluxutil_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
libfluxutil_la_LDFLAGS = \
	-avoid-version \
	$(AM_LDFLAGS)
