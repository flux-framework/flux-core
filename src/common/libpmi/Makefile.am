AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = \
	libpmi_client.la \
	libpmi_server.la \
	libpmi_common.la \
	libupmi.la

libpmi_common_la_SOURCES = \
	pmi_strerror.c \
	pmi_strerror.h \
	keyval.c \
	keyval.h \
	sentinel.c

libpmi_client_la_SOURCES = \
	simple_client.c \
	simple_client.h \
	pmi.c \
	pmi2.c

libpmi_server_la_SOURCES = \
	simple_server.h \
	simple_server.c

libupmi_la_SOURCES = \
	upmi.h \
	upmi.c \
	upmi_plugin.h \
	upmi_simple.c \
	upmi_libpmi.c \
	upmi_libpmi2.c \
	upmi_single.c \
	bizcard.c \
	bizcard.h

fluxinclude_HEADERS = \
	pmi.h \
	pmi2.h

TESTS = test_keyval.t \
	test_simple.t \
	test_canonical.t \
	test_canonical2.t \
	test_upmi.t \
	test_bizcard.t

test_ldadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libpmi/libpmi_client.la \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libpmi/libpmi_common.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(JANSSON_LIBS) \
	$(LIBPTHREAD) \
	$(LIBRT) \
	$(LIBDL)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	-DUPMI_TEST_SEARCHPATH=\"$(builddir)/test/plugin/.libs\" \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

check_PROGRAMS = \
	$(TESTS) \
	test_pmi_info \
	test_pmi2_info \
	test_kvstest \
	test_kvstest2

check_LTLIBRARIES = \
        test/plugin/singlex.la

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

test_upmi_t_SOURCES = test/upmi.c
test_upmi_t_CPPFLAGS = $(test_cppflags)
test_upmi_t_LDADD = \
	$(top_builddir)/src/common/libpmi/libupmi.la \
	$(test_ldadd)

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


test_plugin_singlex_la_SOURCES = test/plugin/singlex.c
test_plugin_singlex_la_CPPFLAGS = $(test_cppflags)
test_plugin_singlex_la_LIBADD = \
	$(top_builddir)/src/common/libpmi/libupmi.la \
	$(test_ldadd)
test_plugin_singlex_la_LDFLAGS = -module -rpath /nowhere $(test_ldflags)

test_bizcard_t_SOURCES = test/bizcard.c
test_bizcard_t_CPPFLAGS = $(test_cppflags)
test_bizcard_t_LDADD = \
	$(top_builddir)/src/common/libpmi/libupmi.la \
	$(test_ldadd)

EXTRA_DIST = \
    ltrace.conf
