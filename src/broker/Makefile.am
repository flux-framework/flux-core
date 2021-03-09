AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(PMIX_CFLAGS) \
	$(ZMQ_CFLAGS) \
	$(LIBUUID_CFLAGS) \
	$(VALGRIND_CFLAGS)

fluxcmd_PROGRAMS = flux-broker

noinst_LTLIBRARIES = libbroker.la

flux_broker_SOURCES = \
	broker.c \
	broker.h

libbroker_la_SOURCES = \
	brokercfg.c \
	brokercfg.h \
	module.c \
	module.h \
	modservice.c \
	modservice.h \
	overlay.h \
	overlay.c \
	service.h \
	service.c \
	attr.h \
	attr.c \
	log.h \
	log.c \
	content-cache.h \
	content-cache.c \
	runat.h \
	runat.c \
	state_machine.h \
	state_machine.c \
	heaptrace.h \
	heaptrace.c \
	exec.h \
	exec.c \
	ping.h \
	ping.c \
	rusage.h \
	rusage.c \
	boot_config.h \
	boot_config.c \
	boot_pmi.h \
	boot_pmi.c \
	pmiutil.h \
	pmiutil.c \
	liblist.h \
	liblist.c \
	publisher.h \
	publisher.c

flux_broker_LDADD = \
	$(builddir)/libbroker.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libpmi/libpmi_client.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(PMIX_LIBS)

flux_broker_LDFLAGS =

TESTS = test_attr.t \
	test_service.t \
	test_liblist.t \
	test_pmiutil.t \
	test_boot_config.t \
	test_runat.t \
	test_overlay.t

test_ldadd = \
	$(builddir)/libbroker.la \
	$(top_builddir)/src/common/libtestutil/libtestutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libpmi/libpmi_client.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(PMIX_LIBS)

test_ldflags = \
	-no-install

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)


check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_attr_t_SOURCES = test/attr.c
test_attr_t_CPPFLAGS = $(test_cppflags)
test_attr_t_LDADD = $(test_ldadd)
test_attr_t_LDFLAGS = $(test_ldflags)

test_service_t_SOURCES = test/service.c
test_service_t_CPPFLAGS = $(test_cppflags)
test_service_t_LDADD = $(test_ldadd)
test_service_t_LDFLAGS = $(test_ldflags)

test_liblist_t_SOURCES = test/liblist.c
test_liblist_t_CPPFLAGS = $(test_cppflags)
test_liblist_t_LDADD = $(test_ldadd)
test_liblist_t_LDFLAGS = $(test_ldflags)

test_pmiutil_t_SOURCES = test/pmiutil.c
test_pmiutil_t_CPPFLAGS = $(test_cppflags)
test_pmiutil_t_LDADD = $(test_ldadd)
test_pmiutil_t_LDFLAGS = $(test_ldflags)

test_boot_config_t_SOURCES = test/boot_config.c
test_boot_config_t_CPPFLAGS = $(test_cppflags)
test_boot_config_t_LDADD = $(test_ldadd)
test_boot_config_t_LDFLAGS = $(test_ldflags)

test_runat_t_SOURCES = test/runat.c
test_runat_t_CPPFLAGS = $(test_cppflags)
test_runat_t_LDADD = $(test_ldadd)
test_runat_t_LDFLAGS = $(test_ldflags)

test_overlay_t_SOURCES = test/overlay.c
test_overlay_t_CPPFLAGS = $(test_cppflags)
test_overlay_t_LDADD = $(test_ldadd)
test_overlay_t_LDFLAGS = $(test_ldflags)
