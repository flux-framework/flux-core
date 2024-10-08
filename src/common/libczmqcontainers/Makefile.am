AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include

noinst_LTLIBRARIES = \
	libczmqcontainers.la

libczmqcontainers_la_SOURCES = \
	czmq_containers.h \
	czmq_internal.h \
	czmq_internal.c \
	czmq_rename.h \
	zhashx.h \
	zhashx.c \
	zhash_primes.inc \
	zlistx.h \
	zlistx.c \
	zhash.h \
	zhash.c \
	zlist.h \
	zlist.c
