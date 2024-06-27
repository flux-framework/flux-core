AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(LIBUUID_CFLAGS)

noinst_LTLIBRARIES = libfluxutil.la
libfluxutil_la_SOURCES = \
	method.h \
	method.c \
	policy.h \
	policy.c \
	msg_hash.h \
	msg_hash.c

TESTS = test_msg_hash.t

test_ldadd = \
	$(top_builddir)/src/common/libfluxutil/libfluxutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(LIBUUID_LIBS)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_msg_hash_t_SOURCES = test/msg_hash.c
test_msg_hash_t_CPPFLAGS = $(test_cppflags)
test_msg_hash_t_LDADD = $(test_ldadd)
test_msg_hash_t_LDFLAGS = $(test_ldflags)
