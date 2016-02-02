AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = libpmi-client.la

libpmi_client_la_SOURCES = \
	pmi-client.h \
	pmi-impl.h \
	pmi.c \
	pmi-dlopen.c \
	pmi-simple.c
