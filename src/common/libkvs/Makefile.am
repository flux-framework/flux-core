AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libkvs.la

libkvs_la_SOURCES = \
	kvs.c \
	kvs_lookup.c \
	kvs_getroot.c \
	kvs_dir.c \
	kvs_dir_private.h \
	kvs_classic.c \
	kvs_watch.c \
	kvs_commit.c \
	kvs_txn.c \
	kvs_txn_private.h \
	treeobj.h \
	treeobj.c \
	kvs_eventlog.c \
	kvs_copy.c

fluxcoreinclude_HEADERS = \
	kvs.h \
	kvs_lookup.h \
	kvs_getroot.h \
	kvs_dir.h \
	kvs_watch.h \
	kvs_classic.h \
	kvs_txn.h \
	kvs_commit.h \
	kvs_eventlog.h \
	kvs_copy.h

TESTS = \
	test_kvs.t \
	test_kvs_txn.t \
	test_kvs_lookup.t \
	test_kvs_dir.t \
	test_kvs_commit.t \
	test_kvs_watch.t \
	test_kvs_getroot.t \
	test_treeobj.t \
	test_kvs_eventlog.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(ZMQ_LIBS) $(JANSSON_LIBS) $(LIBPTHREAD) $(LIBRT) $(LIBDL)

test_cppflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_kvs_t_SOURCES = test/kvs.c
test_kvs_t_CPPFLAGS = $(test_cppflags)
test_kvs_t_LDADD = $(test_ldadd) $(LIBDL)

test_kvs_txn_t_SOURCES = test/kvs_txn.c
test_kvs_txn_t_CPPFLAGS = $(test_cppflags)
test_kvs_txn_t_LDADD = $(test_ldadd) $(LIBDL)

test_kvs_lookup_t_SOURCES = test/kvs_lookup.c
test_kvs_lookup_t_CPPFLAGS = $(test_cppflags)
test_kvs_lookup_t_LDADD = \
	$(top_builddir)/src/common/libkvs/kvs_txn.o \
	$(test_ldadd) \
	$(LIBDL)

test_kvs_dir_t_SOURCES = test/kvs_dir.c
test_kvs_dir_t_CPPFLAGS = $(test_cppflags)
test_kvs_dir_t_LDADD = $(test_ldadd) $(LIBDL)

test_kvs_commit_t_SOURCES = test/kvs_commit.c
test_kvs_commit_t_CPPFLAGS = $(test_cppflags)
test_kvs_commit_t_LDADD = $(test_ldadd) $(LIBDL)

test_kvs_watch_t_SOURCES = test/kvs_watch.c
test_kvs_watch_t_CPPFLAGS = $(test_cppflags)
test_kvs_watch_t_LDADD = $(test_ldadd) $(LIBDL)

test_kvs_getroot_t_SOURCES = test/kvs_getroot.c
test_kvs_getroot_t_CPPFLAGS = $(test_cppflags)
test_kvs_getroot_t_LDADD = $(test_ldadd) $(LIBDL)

test_treeobj_t_SOURCES = test/treeobj.c
test_treeobj_t_CPPFLAGS = $(test_cppflags)
test_treeobj_t_LDADD = $(test_ldadd) $(LIBDL)

test_kvs_eventlog_t_SOURCES = test/kvs_eventlog.c
test_kvs_eventlog_t_CPPFLAGS = $(test_cppflags)
test_kvs_eventlog_t_LDADD = $(test_ldadd) $(LIBDL)
