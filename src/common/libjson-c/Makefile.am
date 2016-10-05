AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir)

noinst_LTLIBRARIES = libjson-c.la

libjson_c_la_SOURCES = \
	bits.h \
	arraylist.c \
	arraylist.h \
	debug.c \
	debug.h \
	json_c_version.c \
	json_c_version.h \
	json_object.c \
	json_object.h \
	json_object_private.h \
	json_object_iterator.c \
	json_object_iterator.h \
	json_tokener.c \
	json_tokener.h \
	json_util.c \
	json_util.h \
	linkhash.c \
	linkhash.h \
	printbuf.c \
	printbuf.h \
	random_seed.c \
	random_seed.h \
	json.h \
	json_inttypes.h

EXTRA_DIST = AUTHORS COPYING
