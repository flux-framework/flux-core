AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS) \
	$(JANSSON_CFLAGS) \
	$(HWLOC_CFLAGS)

noinst_LTLIBRARIES = \
	librlist.la

librlist_la_SOURCES = \
	rnode.h \
	rnode.c \
	rlist.c \
	rlist.h

test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(ZMQ_LIBS) \
	$(LIBPTHREAD) \
	$(JANSSON_LIBS) \
	$(HWLOC_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

test_ldflags = \
	-no-install

TESTS = \
	test_rnode.t \
	test_rlist.t

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

test_rlist_t_SOURCES = \
	test/rlist.c
test_rlist_t_CPPFLAGS = \
	$(test_cppflags)
test_rlist_t_LDADD = \
	librlist.la \
	$(test_ldadd)
test_rlist_t_LDFLAGS = \
	$(test_ldflags)
