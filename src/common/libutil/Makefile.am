AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/liblsd

noinst_LTLIBRARIES = libutil.la

libutil_la_SOURCES = \
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
	zmsg.c \
	zmsg.h \
	zdump.c \
	zdump.h \
	zconnect.c \
	zconnect.h \
	zfd.c \
	zfd.h \
	lsdfatal.c \
	optparse.c \
	optparse.h \
	tstat.c \
	tstat.h \
	veb.c \
	veb.h \
	nodeset.c \
	nodeset.h \
	shortjson.h

libutil_la_LIBADD = \
	$(top_builddir)/src/common/liblsd/liblsd.la

EXTRA_DIST = veb_mach.c
