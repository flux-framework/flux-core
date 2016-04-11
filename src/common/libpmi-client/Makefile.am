AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libpmi-client.la

libpmi_client_la_SOURCES = \
	pmi-client.h \
	pmi-impl.h \
	pmi.c \
	pmi-dlopen.c \
	pmi-simple.c
