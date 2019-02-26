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

fluxmod_LTLIBRARIES = \
	sched-simple.la

noinst_LTLIBRARIES = \
	libjj.la

noinst_PROGRAMS = \
	rlist-query

libjj_la_SOURCES = \
	libjj.h \
	libjj.c

sched_simple_la_SOURCES = \
	sched.c \
	rnode.c \
	rnode.h \
	rlist.c \
	rlist.h

sched_simple_la_LDFLAGS = \
	$(fluxmod_ldflags) \
	-module

sched_simple_la_LIBADD = \
	$(fluxmod_ldadd) \
	libjj.la \
	$(top_builddir)/src/common/libschedutil/libschedutil.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(ZMQ_LIBS)

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
	$(TESTS) \
	rlist-query

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

rlist_query_SOURCES = \
	rnode.c \
	rnode.h \
	rlist.c \
	rlist.h \
	test/rlist-query.c
rlist_query_CPPFLAGS = \
	$(test_cppflags)
rlist_query_LDADD = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(JANSSON_LIBS)
