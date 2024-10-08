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
	$(FLUX_SECURITY_CFLAGS) \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libingest.la

libingest_la_SOURCES = \
	job-ingest.c \
	workcrew.c \
	workcrew.h \
	worker.c \
	worker.h \
	types.h \
	util.h \
	util.c \
	job.h \
	job.c \
	pipeline.h \
	pipeline.c

TESTS = \
	test_util.t \
	test_job.t

test_ldadd = \
	$(builddir)/libingest.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(JANSSON_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_util_t_SOURCES = test/util.c
test_util_t_CPPFLAGS = $(test_cppflags)
test_util_t_LDADD = $(test_ldadd)
test_util_t_LDFLAGS = $(test_ldflags)

test_job_t_SOURCES = test/job.c
test_job_t_CPPFLAGS = $(test_cppflags)
test_job_t_LDADD = $(test_ldadd)
test_job_t_LDFLAGS = $(test_ldflags)
