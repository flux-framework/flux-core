AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-Wno-strict-aliasing \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libutil.la

libutil_la_SOURCES = \
	ipaddr.c \
	ipaddr.h \
	log.c \
	log.h \
	xzmalloc.c \
	xzmalloc.h \
	jsonutil.c \
	jsonutil.h \
	monotime.c \
	monotime.h \
	setenvf.c \
	setenvf.h \
	argv.c \
	argv.h \
	env.c \
	env.h \
	zdump.c \
	zdump.h \
	zconnect.c \
	zconnect.h \
	zfd.c \
	zfd.h \
	optparse.c \
	optparse.h \
	tstat.c \
	tstat.h \
	veb.c \
	veb.h \
	nodeset.c \
	nodeset.h \
	shortjson.h \
	readall.c \
	readall.h \
	subprocess.c \
	subprocess.h \
	ev_zmq.c \
	ev_zmq.h \
	ev_zlist.c \
	ev_zlist.h

EXTRA_DIST = veb_mach.c

TESTS = test_nodeset.t \
	test_optparse.t \
	test_subprocess.t \
	test_ev.t

test_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD)

test_cppflags = \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

check_PROGRAMS = $(TESTS)

TEST_EXTENSIONS = .t
T_LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
       $(top_srcdir)/config/tap-driver.sh

test_nodeset_t_SOURCES = test/nodeset.c
test_nodeset_t_CPPFLAGS = $(test_cppflags)
test_nodeset_t_LDADD = $(test_ldadd)

test_optparse_t_SOURCES = test/optparse.c
test_optparse_t_CPPFLAGS = $(test_cppflags)
test_optparse_t_LDADD = $(test_ldadd)

test_subprocess_t_SOURCES = test/subprocess.c
test_subprocess_t_CPPFLAGS = $(test_cppflags)
test_subprocess_t_LDADD = $(test_ldadd)

test_ev_t_SOURCES = test/ev.c
test_ev_t_CPPFLAGS = $(test_cppflags)
test_ev_t_LDADD = $(test_ldadd)
