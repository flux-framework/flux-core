AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libcjson.la

libcjson_la_SOURCES = \
	cJSON.c \
	cJSON_Utils.c \
	cJSON.h \
	cJSON_Utils.h

EXTRA_DIST = LICENSE
