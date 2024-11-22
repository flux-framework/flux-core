AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
        -I$(top_srcdir) \
        -I$(top_srcdir)/src/include \
        -I$(top_srcdir)/src/common/libccan \
        -I$(top_builddir)/src/common/libflux \
        $(JANSSON_CFLAGS) \
        $(LIBARCHIVE_CFLAGS)

noinst_LTLIBRARIES = \
        libfilemap.la

libfilemap_la_SOURCES = \
        filemap.h \
        filemap.c \
        fileref.h \
        fileref.c

TESTS = \
	test_fileref.t

test_ldadd = \
        $(top_builddir)/src/common/libfilemap/libfilemap.la \
        $(top_builddir)/src/common/libutil/libutil.la \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(top_builddir)/src/common/libccan/libccan.la \
        $(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
        $(JANSSON_LIBS)

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_fileref_t_SOURCES = test/fileref.c
test_fileref_t_CPPFLAGS = $(test_cppflags)
test_fileref_t_LDADD = $(test_ldadd)
