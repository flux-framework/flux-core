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

noinst_LTLIBRARIES = libidset.la
fluxinclude_HEADERS = idset.h
libidset_la_SOURCES = idset.c \
		      idset_private.h \
		      idset_decode.c \
		      idset_encode.c \
		      idset_format.c \
		      veb.c \
		      veb.h

EXTRA_DIST = veb_mach.c

TESTS = test_veb.t \
	test_idset.t

check_PROGRAMS = \
	$(TESTS) \
	test_idsetutil

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_veb_t_SOURCES = test/veb.c
test_veb_t_CPPFLAGS = $(AM_CPPFLAGS)
test_veb_t_LDADD = \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la

test_idset_t_SOURCES = test/idset.c
test_idset_t_CPPFLAGS = $(AM_CPPFLAGS)
test_idset_t_LDADD = \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la

test_idsetutil_SOURCES = test/idsetutil.c
test_idsetutil_CPPFLAGS = $(AM_CPPFLAGS)
test_idsetutil_LDADD = \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(top_builddir)/src/common/libutil/libutil.la
