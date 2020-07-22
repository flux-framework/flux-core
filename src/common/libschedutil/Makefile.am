AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS)

noinst_LTLIBRARIES = \
	libschedutil.la

fluxschedutilinclude_HEADERS = init.h \
	ready.h \
	ops.h \
	alloc.h \
	free.h

libschedutil_la_SOURCES = \
	schedutil_private.h \
	init.h \
	init.c \
	hello.c \
	ready.h \
	ready.c \
	ops.h \
	ops.c \
	alloc.h \
	alloc.c \
	free.h \
	free.c \
	resource.c

libschedutil_la_LIBADD = \
	$(ZMQ_LIBS)
