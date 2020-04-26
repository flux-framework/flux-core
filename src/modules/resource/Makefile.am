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
fluxmod_LTLIBRARIES = resource.la

resource_la_SOURCES = \
	resource.c \
	resource.h \
	monitor.c \
	monitor.h \
	discover.c \
	discover.h \
	drain.c \
	drain.h \
	exclude.c \
	exclude.h \
	reslog.c \
	reslog.h \
	acquire.c \
	acquire.h \
	rutil.c \
	rutil.h

resource_la_LDFLAGS = $(fluxmod_ldflags) -module
resource_la_LIBADD = $(fluxmod_libadd) \
		    $(top_builddir)/src/common/libflux-internal.la \
		    $(top_builddir)/src/common/libflux-core.la \
		    $(ZMQ_LIBS)
