AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(JANSSON_CFLAGS)

noinst_LTLIBRARIES = libsdexec.la

libsdexec_la_SOURCES = \
	state.c \
	state.h \
	start.c \
	start.h \
	stop.h \
	stop.c \
	list.h \
	list.c \
	property.h \
	property.c \
	channel.h \
	channel.c \
	unit.h \
	unit.c \
	parse.h \
	parse.c
