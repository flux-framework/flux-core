AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

lib_LTLIBRARIES = libflux-mrpc.la
fluxcoreinclude_HEADERS = \
		      mrpc.h

libflux_mrpc_la_SOURCES = mrpc.c \
			  mrpc.h \
			  mrpc_deprecated.h

libflux_mrpc_la_LDFLAGS = $(fluxlib_ldflags) \
			  -export-symbols-regex '^flux_.*'

libflux_mrpc_la_LIBADD = $(top_builddir)/src/common/libflux-internal.la \
			 $(top_builddir)/src/common/libflux-core.la \
			 $(top_builddir)/src/modules/kvs/libflux-kvs.la \
			 $(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) \
			 $(LIBPTHREAD) $(LIBUTIL) $(LIBDL) $(LIBRT)
