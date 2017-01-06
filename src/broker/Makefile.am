AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

fluxcmd_PROGRAMS = flux-broker

flux_broker_SOURCES = \
	broker.c \
	module.c \
	module.h \
	modservice.c \
	modservice.h \
	overlay.h \
	overlay.c \
	heartbeat.h \
	heartbeat.c \
	snoop.h \
	snoop.c \
	service.h \
	service.c \
	hello.h \
	hello.c \
	shutdown.h \
	shutdown.c \
	attr.h \
	attr.c \
	sequence.h \
	sequence.c \
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
	rusage.c

flux_broker_LDADD = \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la 

broker_ldflags = 

if HAVE_PYTHON
#allow the broker to find the python library at runtime
broker_ldflags += -rpath $(PYTHON_PREFIX)/lib
endif

flux_broker_LDFLAGS = ${broker_ldflags}

TESTS = test_snoop.t \
	test_shutdown.t \
	test_heartbeat.t \
	test_hello.t \
	test_attr.t

test_ldadd = \
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

test_snoop_t_SOURCES = test/snoop.c snoop.c
test_snoop_t_CPPFLAGS = $(test_cppflags)
test_snoop_t_LDADD = $(test_ldadd)

test_shutdown_t_SOURCES = test/shutdown.c shutdown.c
test_shutdown_t_CPPFLAGS = $(test_cppflags)
test_shutdown_t_LDADD = $(test_ldadd)

test_heartbeat_t_SOURCES = test/heartbeat.c heartbeat.c attr.c
test_heartbeat_t_CPPFLAGS = $(test_cppflags)
test_heartbeat_t_LDADD = $(test_ldadd)

test_hello_t_SOURCES = test/hello.c hello.c attr.c
test_hello_t_CPPFLAGS = $(test_cppflags)
test_hello_t_LDADD = $(test_ldadd)

test_attr_t_SOURCES = test/attr.c attr.c
test_attr_t_CPPFLAGS = $(test_cppflags)
test_attr_t_LDADD = $(test_ldadd)
