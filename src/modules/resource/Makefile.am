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
	$(JANSSON_CFLAGS) \
	$(HWLOC_CFLAGS)

noinst_LTLIBRARIES = libresource.la

libresource_la_SOURCES = \
	resource.c \
	resource.h \
	monitor.c \
	monitor.h \
	topo.c \
	topo.h \
	drain.c \
	drain.h \
	exclude.c \
	exclude.h \
	reslog.c \
	reslog.h \
	acquire.c \
	acquire.h \
	inventory.c \
	inventory.h \
	rutil.c \
	rutil.h \
	status.c \
	status.h \
	drainset.h \
	drainset.c

TESTS = \
	test_rutil.t \
	test_drainset.t

test_ldadd = \
	$(builddir)/libresource.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(HWLOC_LIBS) \
	$(JANSSON_LIBS) \
	$(LIBPTHREAD)

test_ldflags = \
        -no-install

test_cppflags = \
        $(AM_CPPFLAGS) \
	$(JANSSON_CFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_rutil_t_SOURCES = test/rutil.c
test_rutil_t_CPPFLAGS = $(test_cppflags)
test_rutil_t_LDADD = $(test_ldadd)
test_rutil_t_LDFLAGS = $(test_ldflags)

test_drainset_t_SOURCES = test/drainset.c
test_drainset_t_CPPFLAGS = $(test_cppflags)
test_drainset_t_LDADD = $(test_ldadd)
test_drainset_t_LDFLAGS = $(test_ldflags)
