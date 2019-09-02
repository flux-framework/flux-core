AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = \
	librouter.la

librouter_la_SOURCES = \
	sendfd.h \
	sendfd.c \
	auth.c \
	auth.h \
	usock.c \
	usock.h \
	disconnect.h \
	disconnect.c \
	subhash.h \
	subhash.c \
	servhash.h \
	servhash.c

TESTS = \
	test_sendfd.t

check_PROGRAMS = \
        $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/librouter/librouter.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
        $(AM_CPPFLAGS) \
        -I$(top_srcdir)/src/common/libtap

test_sendfd_t_SOURCES = test/sendfd.c
test_sendfd_t_CPPFLAGS = $(test_cppflags)
test_sendfd_t_LDADD = $(test_ldadd)
