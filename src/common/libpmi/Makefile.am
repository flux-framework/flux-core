AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libpmi-simple.la libpmi.la

libpmi_simple_la_SOURCES = \
	simple_server.h \
	simple_server.c

libpmi_la_SOURCES = \
	client.c \
	client.h \
	client_impl.h \
	client_dlopen.c \
	simple_client.c
