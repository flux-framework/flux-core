AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libkvs.la

libkvs_la_SOURCES = \
	kvs.c \
	kvs_lookup.c \
	kvs_dir.c \
	kvs_classic.c \
	kvs_watch.c \
	jansson_dirent.c \
	jansson_dirent.h \
	kvs_commit.c \
	kvs_txn.c \
	kvs_txn_private.h

fluxcoreinclude_HEADERS = \
	kvs.h \
	kvs_lookup.h \
	kvs_dir.h \
	kvs_watch.h \
	kvs_classic.h \
	kvs_txn.h \
	kvs_commit.h

TESTS = \
	test_jansson_dirent.t \
	test_kvs_txn.t

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
	$(ZMQ_LIBS) $(JANSSON_LIBS) $(LIBPTHREAD) $(LIBRT) $(LIBDL) $(LIBMUNGE)

test_cppflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_jansson_dirent_t_SOURCES = test/jansson_dirent.c
test_jansson_dirent_t_CPPFLAGS = $(test_cppflags)
test_jansson_dirent_t_LDADD = $(test_ldadd)

test_kvs_txn_t_SOURCES = test/kvs_txn.c
test_kvs_txn_t_CPPFLAGS = $(test_cppflags)
test_kvs_txn_t_LDADD = $(test_ldadd) $(LIBDL)
