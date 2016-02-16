AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS)

#
# Comms module
#
fluxmod_LTLIBRARIES = kvs.la

kvs_la_SOURCES = \
	kvs.c \
	cache.c \
	cache.h \
	waitqueue.c \
	waitqueue.h \
	proto.c \
	proto.h

kvs_la_LDFLAGS = $(fluxmod_ldflags) -module
kvs_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
		$(top_builddir)/src/common/libflux-core.la \
		$(JSON_LIBS) $(ZMQ_CFLAGS)

#
# API for module
#
lib_LTLIBRARIES = libflux-kvs.la
fluxcoreinclude_HEADERS = kvs.h
libflux_kvs_la_SOURCES = \
	libkvs.c \
	conf.c \
	proto.c \
	kvs_deprecated.h
libflux_kvs_la_LDFLAGS = $(fluxlib_ldflags) \
			 -export-symbols-regex '^(kvs|kvsdir|kvsitr)_.*'
libflux_kvs_la_LIBADD =  $(top_builddir)/src/common/libflux-internal.la \
			 $(top_builddir)/src/common/libflux-core.la

TESTS = \
	test_waitqueue.t \
	test_proto.t

test_ldadd = \
        $(top_builddir)/src/modules/kvs/waitqueue.o \
        $(top_builddir)/src/modules/kvs/proto.o \
        $(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libtap/libtap.la \
        $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD)

test_cppflags = \
        $(AM_CPPFLAGS) \
        -DMODULE_PATH=\"$(top_builddir)/src/modules\" \
        -I$(top_srcdir)/src/common/libtap

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_waitqueue_t_SOURCES = test/waitqueue.c
test_waitqueue_t_CPPFLAGS = $(test_cppflags)
test_waitqueue_t_LDADD = $(test_ldadd)

test_proto_t_SOURCES = test/proto.c
test_proto_t_CPPFLAGS = $(test_cppflags)
test_proto_t_LDADD = $(test_ldadd)
