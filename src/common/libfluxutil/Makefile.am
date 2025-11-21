AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libfluxutil.la
libfluxutil_la_SOURCES = \
	conf_policy.h \
	conf_policy.c \
	conf_bootstrap.h \
	conf_bootstrap.c


TESTS = \
	test_conf_bootstrap.t

check_PROGRAMS = \
        $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_cflags = \
	-Wno-unused-value \
	$(AM_CFLAGS)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	-I$(srcdir) \
	$(AM_CPPFLAGS)

test_ldadd = \
	$(builddir)/libfluxutil.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la
	$(JANSSON_LIBS)

test_ldflags = \
	-no-install

test_conf_bootstrap_t_SOURCES = test/conf_bootstrap.c
test_conf_bootstrap_t_CFLAGS = $(test_cflags)
test_conf_bootstrap_t_CPPFLAGS = $(test_cppflags)
test_conf_bootstrap_t_LDADD = $(test_ldadd)
