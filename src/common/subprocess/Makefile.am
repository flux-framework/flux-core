AM_CFLAGS = \
        $(WARNING_CFLAGS) \
        $(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
        $(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include


noinst_LTLIBRARIES = \
	libsubprocess.la

libsubprocess_la_SOURCES = \
	command.c \
	command.h \
	local.c \
	local.h \
	remote.c \
	remote.h \
	server.c \
	server.h \
	util.c \
	util.h \
	subprocess.c \
	subprocess_private.h

fluxcoreinclude_HEADERS = \
	subprocess.h
