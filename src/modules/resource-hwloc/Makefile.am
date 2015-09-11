AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

#
# Comms module
#
fluxmod_LTLIBRARIES = resource-hwloc.la

fluxmod_libadd = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/modules/kvs/libkvs.la

general_ldflags = --disable-static -avoid-version -shared -export-dynamic \
	$(LIBMUNGE) $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD) $(LIBUTIL) $(HWLOC_LIBS)

fluxmod_ldflags = -module \
	-export-symbols-regex '^mod_(main|name)$$' \
	$(general_ldflags)

resource_hwloc_la_SOURCES = resource.c
resource_hwloc_la_CFLAGS = $(HWLOC_CFLAGS)
resource_hwloc_la_LDFLAGS = $(fluxmod_ldflags)
resource_hwloc_la_LIBADD = $(fluxmod_libadd)
