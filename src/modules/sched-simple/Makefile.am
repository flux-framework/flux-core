AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(JANSSON_CFLAGS)

noinst_LTLIBRARIES = \
	libjj.la

libjj_la_SOURCES = \
	libjj.h \
	libjj.c


test_ldadd = \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(JANSSON_LIBS)

test_cppflags = \
	$(AM_CPPFLAGS)

TESTS = \
	test_rnode.t \
	test_rlist.t

check_PROGRAMS = \
	$(TESTS)

test_rnode_t_SOURCES = \
	rnode.c \
	rnode.h \
	test/rnode.c
test_rnode_t_CPPFLAGS = \
	$(test_cppflags)
test_rnode_t_LDADD = \
	$(test_ldadd)

test_rlist_t_SOURCES = \
	rnode.c \
	rnode.h \
	rlist.c \
	rlist.h \
	test/rlist.c
test_rlist_t_CPPFLAGS = \
	$(test_cppflags)
test_rlist_t_LDADD = \
	$(test_ldadd)

