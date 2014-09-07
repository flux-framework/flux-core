AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-I$(top_srcdir)/src/common/liblsd

noinst_LTLIBRARIES = libutil.la

libutil_la_SOURCES = \
	log.c \
	log.h \
	json.c \
	time.c \
	util.c \
	util.h \
	zmsg.c \
	zmsg.h \
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
