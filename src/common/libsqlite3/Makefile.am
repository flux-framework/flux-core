AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir)

noinst_LTLIBRARIES = libsqlite3.la

libsqlite3_la_SOURCES = \
	sqlite3.h \
	sqlite3.c
