AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS) $(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libjob.la
fluxcoreinclude_HEADERS = job.h

libjob_la_SOURCES = \
	job.c
