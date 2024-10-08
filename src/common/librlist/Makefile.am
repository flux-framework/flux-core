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
	$(JANSSON_CFLAGS) \
	$(HWLOC_CFLAGS)

noinst_LTLIBRARIES = \
	librlist.la

librlist_la_SOURCES = \
	rnode.h \
	rnode.c \
	match.h \
	match.c \
	rlist.c \
	rlist.h \
	rhwloc.c \
	rhwloc.h

test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBPTHREAD) \
	$(JANSSON_LIBS) \
	$(HWLOC_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

TESTS = \
	test_rnode.t \
	test_match.t \
	test_rlist.t \
	test_rhwloc.t

check_PROGRAMS = \
	$(TESTS)

test_rnode_t_SOURCES = \
	test/rnode.c
test_rnode_t_CPPFLAGS = \
	$(test_cppflags)
test_rnode_t_LDADD = \
	librlist.la \
	$(test_ldadd)
test_rnode_t_LDFLAGS = \
	$(test_ldflags)

test_match_t_SOURCES = \
	test/match.c
test_match_t_CPPFLAGS = \
	$(test_cppflags)
test_match_t_LDADD = \
	librlist.la \
	$(test_ldadd)
test_match_t_LDFLAGS = \
	$(test_ldflags)

test_rlist_t_SOURCES = \
	test/rlist.c
test_rlist_t_CPPFLAGS = \
	$(test_cppflags)
test_rlist_t_LDADD = \
	librlist.la \
	$(test_ldadd)
test_rlist_t_LDFLAGS = \
	$(test_ldflags)

test_rhwloc_t_SOURCES = \
	test/rhwloc.c
test_rhwloc_t_CPPFLAGS = \
	$(test_cppflags)
test_rhwloc_t_LDADD = \
	librlist.la \
	$(test_ldadd)
test_rhwloc_t_LDFLAGS = \
	$(test_ldflags)
