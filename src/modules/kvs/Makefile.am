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
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libkvs.la

libkvs_la_SOURCES = \
	kvs.c \
	cache.c \
	cache.h \
	waitqueue.c \
	waitqueue.h \
	lookup.c \
	lookup.h \
	treq.c \
	treq.h \
	kvstxn.c \
	kvstxn.h \
	kvsroot.c \
	kvsroot.h \
	kvs_wait_version.c \
	kvs_wait_version.h \
	kvs_checkpoint.c \
	kvs_checkpoint.h

TESTS = \
	test_waitqueue.t \
	test_cache.t \
	test_lookup.t \
	test_treq.t \
	test_kvstxn.t \
	test_kvsroot.t \
	test_kvs_wait_version.t

test_ldadd = \
	$(builddir)/libkvs.la \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libflux-core.la \
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

test_waitqueue_t_SOURCES = test/waitqueue.c
test_waitqueue_t_CPPFLAGS = $(test_cppflags)
test_waitqueue_t_LDADD = \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(test_ldadd)
test_waitqueue_t_LDFLAGS = \
	$(test_ldflags)

test_cache_t_SOURCES = test/cache.c
test_cache_t_CPPFLAGS = $(test_cppflags)
test_cache_t_LDADD = \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(test_ldadd)
test_cache_t_LDFLAGS = \
	$(test_ldflags)

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
test_lookup_t_LDFLAGS = \
	$(test_ldflags)

test_treq_t_SOURCES = test/treq.c
test_treq_t_CPPFLAGS = $(test_cppflags)
test_treq_t_LDADD = \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(test_ldadd)
test_treq_t_LDFLAGS = \
	$(test_ldflags)

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
test_kvstxn_t_LDFLAGS = \
	$(test_ldflags)

test_kvsroot_t_SOURCES = test/kvsroot.c
test_kvsroot_t_CPPFLAGS = $(test_cppflags)
test_kvsroot_t_LDADD = \
	$(top_builddir)/src/modules/kvs/kvsroot.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/kvstxn.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(test_ldadd)
test_kvsroot_t_LDFLAGS = \
	$(test_ldflags)

test_kvs_wait_version_t_SOURCES = test/kvs_wait_version.c
test_kvs_wait_version_t_CPPFLAGS = $(test_cppflags)
test_kvs_wait_version_t_LDADD = \
	$(top_builddir)/src/modules/kvs/kvs_wait_version.o \
	$(top_builddir)/src/modules/kvs/waitqueue.o \
	$(top_builddir)/src/modules/kvs/kvsroot.o \
	$(top_builddir)/src/modules/kvs/kvstxn.o \
	$(top_builddir)/src/modules/kvs/cache.o \
	$(top_builddir)/src/modules/kvs/treq.o \
	$(test_ldadd)
test_kvs_wait_version_t_LDFLAGS = \
	$(test_ldflags)

EXTRA_DIST = README.md
