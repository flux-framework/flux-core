AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux

fluxmod_LTLIBRARIES = content-files.la

content_files_la_SOURCES = \
	content-files.c \
	filedb.h \
	filedb.c

content_files_la_LDFLAGS = $(fluxmod_ldflags) -module
content_files_la_LIBADD = \
		$(top_builddir)/src/common/libflux-internal.la \
		$(top_builddir)/src/common/libflux-core.la

TESTS = test_filedb.t

test_ldadd = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(LIBPTHREAD)

test_ldflags = \
	-no-install

test_cppflags = $(AM_CPPFLAGS)

check_PROGRAMS = \
	test_load \
	test_store \
	test_filedb.t

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
	$(top_srcdir)/config/tap-driver.sh


test_load_SOURCES = test/load.c
test_load_CPPFLAGS = $(test_cppflags)
test_load_LDADD = $(builddir)/filedb.o $(test_ldadd)
test_load_LDFLAGS = $(test_ldflags)

test_store_SOURCES = test/store.c
test_store_CPPFLAGS = $(test_cppflags)
test_store_LDADD = $(builddir)/filedb.o $(test_ldadd)
test_store_LDFLAGS = $(test_ldflags)

test_filedb_t_SOURCES = test/filedb.c
test_filedb_t_CPPFLAGS = $(test_cppflags)
test_filedb_t_LDADD = $(builddir)/filedb.o $(test_ldadd)
test_filedb_t_LDFLAGS = $(test_ldflags)
