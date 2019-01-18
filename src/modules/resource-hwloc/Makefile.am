AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS)

#
# Comms module
#
fluxmod_LTLIBRARIES = resource-hwloc.la

resource_hwloc_la_SOURCES = \
	resource.c \
	aggregate.c \
	aggregate.h
resource_hwloc_la_CFLAGS = $(AM_CFLAGS) $(HWLOC_CFLAGS)
resource_hwloc_la_LDFLAGS = $(fluxmod_ldflags) -module
resource_hwloc_la_LIBADD = $(top_builddir)/src/common/libflux-core.la \
			   $(top_builddir)/src/common/libflux-internal.la \
			   $(ZMQ_LIBS) $(LIBPTHREAD) \
			   $(LIBUTIL) $(HWLOC_LIBS)
