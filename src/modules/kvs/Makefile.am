AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = kvs.la

fluxmod_libadd = \
	$(JSON_LIBS) $(ZMQ_CFLAGS)

fluxmod_ldflags = -avoid-version -module -shared -export-dynamic \
        -export-symbols-regex '^mod_(main|name)$$'

kvs_la_SOURCES = \
	kvs.c \
	waitqueue.c \
	waitqueue.h \
	proto.c \
	proto.h

kvs_la_LDFLAGS = $(fluxmod_ldflags)
kvs_la_LIBADD = $(fluxmod_libadd)

#
# API for module
#
fluxlib_LTLIBRARIES = libkvs.la
fluxcoreinclude_HEADERS = kvs.h
libkvs_la_SOURCES = \
	libkvs.c \
	conf.c \
	proto.c
libkvs_la_LDFLAGS = -shared -export-dynamic --disable-static \
		    $(top_builddir)/src/common/libflux-internal.la \
		    $(top_builddir)/src/common/libflux-core.la

#-lrt is for clock_gettime, this should be abstracted

TESTS = \
	test_waitqueue.t \
	test_proto.t

test_ldadd = \
        $(top_builddir)/src/modules/kvs/waitqueue.o \
        $(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libtap/libtap.la \
        $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD)

test_cppflags = \
        -DMODULE_PATH=\"$(top_builddir)/src/modules\" \
        -I$(top_srcdir)/src/common/libtap \
        $(AM_CPPFLAGS)

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
