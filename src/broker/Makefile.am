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
	-DABS_TOP_BUILDDIR=\"${abs_top_builddir}\" \
	$(ZMQ_CFLAGS) \
	$(LIBUUID_CFLAGS) \
	$(JANSSON_CFLAGS) \
	$(LIBSYSTEMD_CFLAGS) \
	$(VALGRIND_CFLAGS)

fluxcmd_PROGRAMS = \
	flux-broker \
	flux-module-exec

noinst_LTLIBRARIES = libbroker.la

libbroker_la_SOURCES = \
	brokercfg.c \
	brokercfg.h \
	module_dso.c \
	module_dso.h \
	module.c \
	module.h \
	module_thread.c \
	modhash.c \
	modhash.h \
	modservice.c \
	modservice.h \
	overlay.h \
	overlay.c \
	bizcard.h \
	bizcard.c \
	service.h \
	service.c \
	attr.h \
	attr.c \
	log.h \
	log.c \
	runat.h \
	runat.c \
	state_machine.h \
	state_machine.c \
	heaptrace.h \
	heaptrace.c \
	exec.h \
	exec.c \
	boot_config.h \
	boot_config.c \
	boot_pmi.h \
	boot_pmi.c \
	publisher.h \
	publisher.c \
	shutdown.h \
	shutdown.c \
	topology.h \
	topology.c \
	trace.h \
	trace.c \
	rundir.h \
	rundir.c

flux_broker_SOURCES = \
	broker.c \
	broker.h
flux_broker_LDADD = \
	$(builddir)/libbroker.la \
	$(top_builddir)/src/modules/libmodule_builtins.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/common/libpmi/libupmi.la \
	$(top_builddir)/src/common/libpmi/libpmi_client.la \
	$(top_builddir)/src/common/libpmi/libpmi_common.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(ZMQ_LIBS) \
	$(LIBUUID_LIBS) \
	$(JANSSON_LIBS) \
	$(LIBSYSTEMD_LIBS) \
	$(LIBDL)
flux_broker_LDFLAGS =

flux_module_exec_SOURCES = \
	module_exec.c
flux_module_exec_LDADD = \
	$(builddir)/libbroker.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(LIBUUID_LIBS) \
	$(JANSSON_LIBS) \
	$(LIBDL)
flux_module_exec_LDFLAGS =


TESTS = test_attr.t \
	test_service.t \
	test_boot_config.t \
	test_runat.t \
	test_overlay.t \
	test_topology.t \
	test_bizcard.t

test_ldadd = \
	$(builddir)/libbroker.la \
	$(top_builddir)/src/common/libtestutil/libtestutil.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(ZMQ_LIBS) \
	$(LIBSYSTEMD_LIBS) \
	$(JANSSON_LIBS)

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

test_topology_t_SOURCES = test/topology.c
test_topology_t_CPPFLAGS = $(test_cppflags)
test_topology_t_LDADD = $(test_ldadd)
test_topology_t_LDFLAGS = $(test_ldflags)

test_bizcard_t_SOURCES = test/bizcard.c
test_bizcard_t_CPPFLAGS = $(test_cppflags)
test_bizcard_t_LDADD = $(test_ldadd)
test_bizcard_t_LDFLAGS = $(test_ldflags)

EXTRA_DIST = README.md
