AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS) $(JANSSON_CFLAGS) $(FLUX_SECURITY_CFLAGS)

noinst_LTLIBRARIES = libjob.la
fluxcoreinclude_HEADERS = job.h

libjob_la_SOURCES = \
	job.c \
	sign_none.c \
	sign_none.h

TESTS = \
	test_job.t \
	test_sign_none.t

check_PROGRAMS = \
        $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
        $(top_srcdir)/config/tap-driver.sh

test_ldadd = \
        $(top_builddir)/src/common/libjob/libjob.la \
        $(top_builddir)/src/common/libflux/libflux.la \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libtap/libtap.la \
        $(ZMQ_LIBS) $(JANSSON_LIBS) $(LIBPTHREAD) $(LIBRT) \
        $(FLUX_SECURITY_LIBS) $(LIBDL)

test_cppflags = \
        $(AM_CPPFLAGS) \
        -I$(top_srcdir)/src/common/libtap

test_job_t_SOURCES = test/job.c
test_job_t_CPPFLAGS = $(test_cppflags)
test_job_t_LDADD = $(test_ldadd) $(LIBDL)

test_sign_none_t_SOURCES = test/sign_none.c
test_sign_none_t_CPPFLAGS = $(test_cppflags)
test_sign_none_t_LDADD = $(test_ldadd) $(LIBDL)
