AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

fluxmod_LTLIBRARIES = kvs.la

kvs_la_SOURCES = \
	kvs.c \
	cache.c \
	cache.h \
	waitqueue.c \
	waitqueue.h \
	kvs_util.h \
	kvs_util.c \
	lookup.h \
	lookup.c \
	fence.h \
	fence.c \
	commit.h \
	commit.c \
	msg_cb_handler.h \
	msg_cb_handler.c

kvs_la_LDFLAGS = $(fluxmod_ldflags) -module
kvs_la_LIBADD = $(top_builddir)/src/common/libkvs/libkvs.la \
		$(top_builddir)/src/common/libflux-internal.la \
		$(top_builddir)/src/common/libflux-core.la \
		$(ZMQ_CFLAGS)

TESTS = \
	test_waitqueue.t \
	test_cache.t \
	test_lookup.t \
	test_fence.t \
	test_commit.t \
	test_kvs_util.t \
	test_msg_cb_handler.t

test_ldadd = \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libtap/libtap.la \
        $(ZMQ_LIBS) $(LIBPTHREAD)

test_cppflags = \
        $(AM_CPPFLAGS) \
        -I$(top_srcdir)/src/common/libtap

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_waitqueue_t_SOURCES = test/waitqueue.c
test_waitqueue_t_CPPFLAGS = $(test_cppflags)
test_waitqueue_t_LDADD = \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/msg_cb_handler.o \
	$(top_builddir)/src/modules/kvs/kvs_util.o \
	$(test_ldadd)

test_cache_t_SOURCES = test/cache.c
test_cache_t_CPPFLAGS = $(test_cppflags)
test_cache_t_LDADD = \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/msg_cb_handler.o \
	$(top_builddir)/src/modules/kvs/kvs_util.o \
	$(test_ldadd)

test_lookup_t_SOURCES = test/lookup.c
test_lookup_t_CPPFLAGS = $(test_cppflags)
test_lookup_t_LDADD = \
	$(top_builddir)/src/modules/kvs/lookup.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/msg_cb_handler.o \
	$(top_builddir)/src/modules/kvs/kvs_util.o \
	$(test_ldadd)

test_fence_t_SOURCES = test/fence.c
test_fence_t_CPPFLAGS = $(test_cppflags)
test_fence_t_LDADD = \
	$(top_builddir)/src/modules/kvs/fence.o \
	$(top_builddir)/src/modules/kvs/kvs_util.o \
	$(test_ldadd)

test_commit_t_SOURCES = test/commit.c
test_commit_t_CPPFLAGS = $(test_cppflags)
test_commit_t_LDADD = \
	$(top_builddir)/src/modules/kvs/commit.o \
	$(top_builddir)/src/modules/kvs/fence.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/lookup.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/msg_cb_handler.o \
	$(top_builddir)/src/modules/kvs/kvs_util.o \
	$(test_ldadd)

test_kvs_util_t_SOURCES = test/kvs_util.c
test_kvs_util_t_CPPFLAGS = $(test_cppflags)
test_kvs_util_t_LDADD = \
	$(top_builddir)/src/modules/kvs/kvs_util.o \
	$(test_ldadd)

test_msg_cb_handler_t_SOURCES = test/msg_cb_handler.c
test_msg_cb_handler_t_CPPFLAGS = $(test_cppflags)
test_msg_cb_handler_t_LDADD = \
	$(top_builddir)/src/modules/kvs/msg_cb_handler.o \
	$(test_ldadd)
