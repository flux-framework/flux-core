AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-Wno-strict-aliasing -Wno-error=strict-aliasing

noinst_LTLIBRARIES = libminilzo.la

libminilzo_la_SOURCES = \
	lzoconf.h \
	lzodefs.h \
	minilzo.c \
	minilzo.h

EXTRA_DIST = COPYING README.LZO

TESTS = test_mini.t

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_mini_t_SOURCES = test/mini.c
test_mini_t_LDADD = $(top_builddir)/src/common/libminilzo/libminilzo.la \
		    $(top_builddir)/src/common/libtap/libtap.la
