AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
        -I$(top_srcdir) \
        -I$(top_srcdir)/src/include \
        -I$(top_srcdir)/src/common/libccan \
        -I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = \
        libfilemap.la

libfilemap_la_SOURCES = \
        filemap.h \
        filemap.c
