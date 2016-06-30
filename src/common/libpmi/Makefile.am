AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libpmi-simple.la

libpmi_simple_la_SOURCES = \
	simple_server.h \
	simple_server.c
