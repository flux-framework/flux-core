AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libpmi.la

libpmi_la_SOURCES = \
	simple_server.h \
	simple_server.c \
	simple_client.c \
	simple_client.h \
	pmi.c \
	pmi2.c \
	wrap.c \
	wrap.h \
	single.c \
	single.h \
	pmi_strerror.c \
	pmi_strerror.h \
	keyval.c \
	keyval.h \
	dgetline.c \
	dgetline.h \
	clique.c \
	clique.h \
	pmi_operations.h

fluxinclude_HEADERS = \
	pmi.h \
	pmi2.h

TESTS = test_keyval.t \
	test_simple.t \
	test_single.t \
	test_clique.t

test_ldadd = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libpmi/libpmi.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(ZMQ_LIBS) $(JANSSON_LIBS) $(LIBPTHREAD) $(LIBRT) $(LIBDL) $(LIBMUNGE)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

check_PROGRAMS = \
	$(TESTS) \
	test_pminfo \
	test_kvstest

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	              $(top_srcdir)/config/tap-driver.sh

test_keyval_t_SOURCES = test/keyval.c
test_keyval_t_CPPFLAGS = $(test_cppflags)
test_keyval_t_LDADD = $(test_ldadd)

test_simple_t_SOURCES = test/simple.c
test_simple_t_CPPFLAGS = $(test_cppflags)
test_simple_t_LDADD = $(test_ldadd)

test_single_t_SOURCES = test/single.c
test_single_t_CPPFLAGS = $(test_cppflags)
test_single_t_LDADD = $(test_ldadd)

test_clique_t_SOURCES = test/clique.c
test_clique_t_CPPFLAGS = $(test_cppflags)
test_clique_t_LDADD = $(test_ldadd)

test_pminfo_SOURCES = test/pminfo.c
test_pminfo_CPPFLAGS = $(test_cppflags)
test_pminfo_LDADD = $(test_ldadd)

test_kvstest_SOURCES = test/kvstest.c
test_kvstest_CPPFLAGS = $(test_cppflags)
test_kvstest_LDADD = $(test_ldadd)
