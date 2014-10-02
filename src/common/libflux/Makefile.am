AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

fluxcoreinclude_HEADERS = \
	flux.h \
	handle.h \
	handle_impl.h \
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
	flog.h \
	fconfig.h

noinst_LTLIBRARIES = \
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
	reparent.c \
	fconfig.c
