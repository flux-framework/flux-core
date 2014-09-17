AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)

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
	shortjson.h

EXTRA_DIST = veb_mach.c
