AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	-Wno-parentheses -Wno-error=parentheses

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-DWITH_PTHREADS

noinst_LTLIBRARIES = liblsd.la

liblsd_la_SOURCES = \
	list.c \
	list.h \
	cbuf.c \
	cbuf.h \
	thread.h \
	hash.c \
	hash.h

TESTS = \
	test_hash.t

test_ldadd = \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_hash_t_SOURCES = test/hash.c
test_hash_t_CPPFLAGS = $(test_cppflags)
test_hash_t_LDADD = $(test_ldadd)
