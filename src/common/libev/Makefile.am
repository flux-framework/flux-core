AM_CPPFLAGS = -w $(CODE_COVERAGE_CPPFLAGS)

noinst_LTLIBRARIES = libev.la

libev_la_SOURCES = \
	ev.c \
	ev.h \
	ev_vars.h \
	ev_wrap.h

EXTRA_DIST = \
	ev_select.c \
	ev_poll.c \
	ev_epoll.c \
	ev_linuxaio.c \
	ev_port.c \
	ev_kqueue.c \
	ev_iouring.c \
	libev.m4 \
	LICENSE
