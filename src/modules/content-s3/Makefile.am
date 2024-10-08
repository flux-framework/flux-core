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
	-I$(top_builddir)/src/common/libflux

noinst_LTLIBRARIES = libcontent-s3.la

libcontent_s3_la_SOURCES = \
	content-s3.c \
	s3.c \
	s3.h

test_ldadd = \
	$(builddir)/libcontent-s3.la \
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
