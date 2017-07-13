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
	proto.c \
	proto.h \
	json_dirent.c \
	json_dirent.h \
	kvs_dir.c \
	kvs_classic.c \
	kvs_watch.c \
	jansson_dirent.c \
	jansson_dirent.h

fluxcoreinclude_HEADERS = \
	kvs.h \
	kvs_lookup.h \
	kvs_dir.h \
	kvs_watch.h \
	kvs_classic.h

TESTS = \
	test_proto.t \
	test_dirent.t \
	test_jansson_dirent.t

check_PROGRAMS = \
	$(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh

test_ldadd = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libkvs/proto.o \
	$(top_builddir)/src/common/libkvs/json_dirent.o \
	$(top_builddir)/src/common/libkvs/jansson_dirent.o \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libtap/libtap.la

test_cppflags = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/common/libtap

test_proto_t_SOURCES = test/proto.c
test_proto_t_CPPFLAGS = $(test_cppflags)
test_proto_t_LDADD = $(test_ldadd)

test_dirent_t_SOURCES = test/dirent.c
test_dirent_t_CPPFLAGS = $(test_cppflags)
test_dirent_t_LDADD = $(test_ldadd)

test_jansson_dirent_t_SOURCES = test/jansson_dirent.c
test_jansson_dirent_t_CPPFLAGS = $(test_cppflags)
test_jansson_dirent_t_LDADD = $(test_ldadd)
