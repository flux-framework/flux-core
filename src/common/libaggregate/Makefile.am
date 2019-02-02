AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS) \
        $(ZMQ_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = libaggregate.la

libaggregate_la_SOURCES = \
	aggregate.h \
	aggregate.c
