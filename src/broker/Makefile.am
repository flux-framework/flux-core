AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(VALGRIND_CFLAGS)

fluxcmd_PROGRAMS = flux-broker

noinst_LTLIBRARIES = libbroker.la

flux_broker_SOURCES = \
	broker.c

libbroker_la_SOURCES = \
	module.c \
	module.h \
	modservice.c \
	modservice.h \
	overlay.h \
	overlay.c \
	heartbeat.h \
	heartbeat.c \
	service.h \
	service.c \
	hello.h \
	hello.c \
	reduce.h \
	reduce.c \
	shutdown.h \
	shutdown.c \
	attr.h \
	attr.c \
	log.h \
	log.c \
	content-cache.h \
	content-cache.c \
	runlevel.h \
	runlevel.c \
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
	publisher.h \
	publisher.c

flux_broker_LDADD = \
	$(builddir)/libbroker.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libpmi/libpmi.la

flux_broker_LDFLAGS =

TESTS = test_shutdown.t \
	test_heartbeat.t \
	test_hello.t \
	test_attr.t \
	test_service.t \
	test_reduce.t

test_ldadd = \
	$(builddir)/libbroker.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)


check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_shutdown_t_SOURCES = test/shutdown.c
test_shutdown_t_CPPFLAGS = $(test_cppflags)
test_shutdown_t_LDADD = $(test_ldadd)

test_heartbeat_t_SOURCES = test/heartbeat.c
test_heartbeat_t_CPPFLAGS = $(test_cppflags)
test_heartbeat_t_LDADD = $(test_ldadd)

test_hello_t_SOURCES = test/hello.c
test_hello_t_CPPFLAGS = $(test_cppflags)
test_hello_t_LDADD = $(test_ldadd)

test_attr_t_SOURCES = test/attr.c
test_attr_t_CPPFLAGS = $(test_cppflags)
test_attr_t_LDADD = $(test_ldadd)

test_service_t_SOURCES = test/service.c
test_service_t_CPPFLAGS = $(test_cppflags)
test_service_t_LDADD = $(test_ldadd)

test_reduce_t_SOURCES = test/reduce.c
test_reduce_t_CPPFLAGS = $(test_cppflags)
test_reduce_t_LDADD = $(test_ldadd)
