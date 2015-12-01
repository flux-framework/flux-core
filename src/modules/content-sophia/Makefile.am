AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxmod_LTLIBRARIES = content-sophia.la

content_sophia_la_SOURCES = \
	content-sophia.c

content_sophia_la_LDFLAGS = $(fluxmod_ldflags) -module
content_sophia_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
		$(top_builddir)/src/common/libflux-core.la \
		$(JSON_LIBS) $(ZMQ_CFLAGS)
