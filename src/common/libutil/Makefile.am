AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
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
	readall.h

EXTRA_DIST = veb_mach.c

TESTS = test_nodeset

test_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libtap/libtap.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ)

test_cppflags = \
	-DTEST_MAIN \
	-I$(top_srcdir)/src/common/libtap \
	$(AM_CPPFLAGS)

check_PROGRAMS = test_nodeset

test_nodeset_SOURCES = nodeset.c
test_nodeset_CPPFLAGS = $(test_cppflags)
test_nodeset_LDADD = $(test_ldadd)
