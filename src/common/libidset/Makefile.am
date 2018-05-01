AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = libidset.la
fluxinclude_HEADERS = idset.h
libidset_la_SOURCES = idset.c

libidset_la_CPPFLAGS = \
	$(AM_CPPFLAGS)
libidset_la_LDFLAGS = \
	-avoid-version -module -shared -export-dynamic \
	$(AM_LDFLAGS)
