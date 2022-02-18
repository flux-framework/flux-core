AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = \
	libpmi_client.la \
	libpmi_server.la

libpmi_common_sources = \
	pmi_strerror.c \
	pmi_strerror.h \
	keyval.c \
	keyval.h \
	clique.c \
	clique.h

libpmi_client_la_SOURCES = \
	simple_client.c \
	simple_client.h \
	pmi.c \
	pmi2.c \
	dgetline.c \
	dgetline.h \
	$(libpmi_common_sources)

libpmi_server_la_SOURCES = \
	simple_server.h \
	simple_server.c \
	$(libpmi_common_sources)

fluxinclude_HEADERS = \
	pmi.h \
	pmi2.h

TESTS = test_keyval.t \
	test_simple.t \
	test_canonical.t \
	test_canonical2.t \
	test_clique.t

test_ldadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libpmi/libpmi_client.la \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(JANSSON_LIBS) \
	$(LIBPTHREAD) \
	$(LIBRT) \
	$(LIBDL)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

check_PROGRAMS = \
	$(TESTS) \
	test_pmi_info \
	test_pmi2_info \
	test_kvstest \
	test_kvstest2

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	              $(top_srcdir)/config/tap-driver.sh

test_keyval_t_SOURCES = test/keyval.c
test_keyval_t_CPPFLAGS = $(test_cppflags)
test_keyval_t_LDADD = $(test_ldadd)

test_simple_t_SOURCES = \
	test/simple.c \
	test/server_thread.c \
	test/server_thread.h
test_simple_t_CPPFLAGS = $(test_cppflags)
test_simple_t_LDADD = $(test_ldadd)

test_canonical_t_SOURCES = \
	test/canonical.c \
	test/server_thread.c \
	test/server_thread.h
test_canonical_t_CPPFLAGS = $(test_cppflags)
test_canonical_t_LDADD = $(test_ldadd)

test_canonical2_t_SOURCES = \
	test/canonical2.c \
	test/server_thread.c \
	test/server_thread.h
test_canonical2_t_CPPFLAGS = $(test_cppflags)
test_canonical2_t_LDADD = $(test_ldadd)


test_clique_t_SOURCES = test/clique.c
test_clique_t_CPPFLAGS = $(test_cppflags)
test_clique_t_LDADD = $(test_ldadd)

test_pmi_info_SOURCES = test/pmi_info.c
test_pmi_info_CPPFLAGS = $(test_cppflags)
test_pmi_info_LDADD = $(test_ldadd)

test_pmi2_info_SOURCES = test/pmi2_info.c
test_pmi2_info_CPPFLAGS = $(test_cppflags)
test_pmi2_info_LDADD = $(test_ldadd)

test_kvstest_SOURCES = test/kvstest.c
test_kvstest_CPPFLAGS = $(test_cppflags)
test_kvstest_LDADD = $(test_ldadd)

test_kvstest2_SOURCES = test/kvstest2.c
test_kvstest2_CPPFLAGS = $(test_cppflags)
test_kvstest2_LDADD = $(test_ldadd)

EXTRA_DIST = \
    ltrace.conf
