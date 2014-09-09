AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/modules/live \
	-I$(top_srcdir)/src/modules/modctl

libfluxdir=$(libdir)/flux
includefluxdir=$(includedir)/flux
ldconfdir=/etc/ld.so.conf.d

includeflux_HEADERS = \
	flux.h \
	handle.h \
	reactor.h \
	reduce.h \
	security.h \
	message.h \
	panic.h \
	event.h \
	request.h \
	module.h \
	reparent.h \
	info.h \
	flog.h

libflux_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
	flog.c \
	info.c \
	handle.c \
	reduce.c \
	security.c \
	message.c \
	panic.c \
	event.c \
	request.c \
	module.c \
	reparent.c

libflux_la_LIBADD = \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD)

libflux_la_LDFLAGS = -Wl,--version-script=$(srcdir)/flux_version.map

if WITH_PKG_CONFIG
pkgconfig_DATA = libflux.pc
endif

ldconf_DATA = libflux.conf

EXTRA_DIST = flux_version.map
