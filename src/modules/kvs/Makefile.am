AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS)

fluxmod_LTLIBRARIES = kvs.la

kvs_la_SOURCES = \
	kvs.c \
	cache.c \
	cache.h \
	waitqueue.c \
	waitqueue.h \
	lookup.h \
	lookup.c \
	treq.h \
	treq.c \
	kvstxn.h \
	kvstxn.c \
	kvsroot.h \
	kvsroot.c \
	kvssync.h \
	kvssync.c

kvs_la_LDFLAGS = $(fluxmod_ldflags) -module
kvs_la_LIBADD = $(top_builddir)/src/common/libkvs/libkvs.la \
		$(top_builddir)/src/common/libflux-internal.la \
		$(top_builddir)/src/common/libflux-core.la \
		$(ZMQ_LIBS)

TESTS = \
	test_waitqueue.t \
	test_cache.t \
	test_lookup.t \
	test_treq.t \
	test_kvstxn.t \
	test_kvsroot.t \
	test_kvssync.t

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
	$(test_ldadd)

test_cache_t_SOURCES = test/cache.c
test_cache_t_CPPFLAGS = $(test_cppflags)
test_cache_t_LDADD = \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(test_ldadd)

test_lookup_t_SOURCES = test/lookup.c
test_lookup_t_CPPFLAGS = $(test_cppflags)
test_lookup_t_LDADD = \
	$(top_builddir)/src/modules/kvs/lookup.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/kvsroot.o \
	$(top_builddir)/src/modules/kvs/kvstxn.o \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(test_ldadd)

test_treq_t_SOURCES = test/treq.c
test_treq_t_CPPFLAGS = $(test_cppflags)
test_treq_t_LDADD = \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(test_ldadd)

test_kvstxn_t_SOURCES = test/kvstxn.c
test_kvstxn_t_CPPFLAGS = $(test_cppflags)
test_kvstxn_t_LDADD = \
	$(top_builddir)/src/modules/kvs/kvstxn.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/lookup.o \
	$(top_builddir)/src/modules/kvs/kvsroot.o \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(test_ldadd)

test_kvsroot_t_SOURCES = test/kvsroot.c
test_kvsroot_t_CPPFLAGS = $(test_cppflags)
test_kvsroot_t_LDADD = \
	$(top_builddir)/src/modules/kvs/kvsroot.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/kvstxn.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(test_ldadd)

test_kvssync_t_SOURCES = test/kvssync.c
test_kvssync_t_CPPFLAGS = $(test_cppflags)
test_kvssync_t_LDADD = \
	$(top_builddir)/src/modules/kvs/kvssync.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/kvsroot.o \
	$(top_builddir)/src/modules/kvs/kvstxn.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(test_ldadd)
