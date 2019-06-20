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
	libiobuf.la

libiobuf_la_SOURCES = \
	iobuf.h \
	iobuf.c

TESTS = \
	test_iobuf.t \
	test_iobuf_iter.t \
	test_iobuf_eof_cb.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/libiobuf/libiobuf.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
        $(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_iobuf_t_SOURCES = test/iobuf.c
test_iobuf_t_CPPFLAGS = $(test_cppflags)
test_iobuf_t_LDADD = $(test_ldadd)

test_iobuf_iter_t_SOURCES = test/iobuf_iter.c
test_iobuf_iter_t_CPPFLAGS = $(test_cppflags)
test_iobuf_iter_t_LDADD = $(test_ldadd)

test_iobuf_eof_cb_t_SOURCES = test/iobuf_eof_cb.c
test_iobuf_eof_cb_t_CPPFLAGS = $(test_cppflags)
test_iobuf_eof_cb_t_LDADD = $(test_ldadd)
