AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
    -I/usr/local/include/prometheus \
	-lprom -lpromhttp -lmicrohttpd

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

# TODO: the last variable needs to be programmatic from configure.ac
AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_srcdir)/src/common/libccan \
	-I$(top_builddir)/src/common/libflux \
    -I/usr/local/include/prometheus \
	-lprom -lpromhttp -lmicrohttpd

noinst_LTLIBRARIES = libprometheus.la

libprometheus_la_SOURCES = \
	prometheus.c

test_ldadd = \
	$(builddir)/libprometheus.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(LIBPTHREAD) $(LIBS3)

test_ldflags = \
	-no-install

test_cppflags = $(AM_CPPFLAGS)

check_PROGRAMS = \
	test_load \
	test_store

test_load_SOURCES = test/load.c
test_load_CPPFLAGS = $(test_cppflags)
test_load_LDADD = $(test_ldadd)
test_load_LDFLAGS = $(test_ldflags)

test_store_SOURCES = test/store.c
test_store_CPPFLAGS = $(test_cppflags)
test_store_LDADD = $(test_ldadd)
test_store_LDFLAGS = $(test_ldflags)