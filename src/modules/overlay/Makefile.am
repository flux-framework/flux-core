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
	$(ZMQ_CFLAGS) \
	$(LIBUUID_CFLAGS) \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = liboverlay.la

liboverlay_la_SOURCES = \
	main.c \
	topology.h \
	topology.c \
	compat.h \
	compat.c

# overlay.h overlay.c
# boot_pmi.h boot_pmi.c
# boot_config.h boot_config.c

TESTS = \
	test_topology.t

#	test_overlay.t

test_ldadd = \
	$(builddir)/liboverlay.la \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(JANSSON_LIBS) \
        $(LIBPTHREAD)

test_ldflags = \
	-no-install

test_cppflags = \
        $(AM_CPPFLAGS) \
        -I$(top_srcdir)/src/common/libtap \
	$(JANSSON_CFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

#test_overlay_t_SOURCES = test/overlay.c
#test_overlay_t_CPPFLAGS = $(test_cppflags)
#test_overlay_t_LDADD = $(test_ldadd)
#test_overlay_t_LDFLAGS = $(test_ldflags)

test_topology_t_SOURCES = test/topology.c
test_topology_t_CPPFLAGS = $(test_cppflags)
test_topology_t_LDADD = $(test_ldadd)
test_topology_t_LDFLAGS = $(test_ldflags)
