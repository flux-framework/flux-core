AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(FLUX_SECURITY_CFLAGS) \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libingest.la

libingest_la_SOURCES = \
	workcrew.c \
	workcrew.h \
	worker.c \
	worker.h \
	util.h \
	util.c

fluxmod_LTLIBRARIES = job-ingest.la

job_ingest_la_SOURCES = \
	job-ingest.c

job_ingest_la_LDFLAGS = $(fluxmod_ldflags) -module
job_ingest_la_LIBADD = \
	$(builddir)/libingest.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(JANSSON_LIBS) \
	$(FLUX_SECURITY_LIBS)

fluxschemadir = $(datadir)/flux/schema/jobspec/
dist_fluxschema_DATA = \
	schemas/jobspec.jsonschema \
	schemas/jobspec_v1.jsonschema

TESTS = \
	test_util.t

test_ldadd = \
	$(builddir)/libingest.la \
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
