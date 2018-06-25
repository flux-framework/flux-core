AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	-Wno-parentheses -Wno-error=parentheses

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-DWITH_PTHREADS

noinst_LTLIBRARIES = liblsd.la

liblsd_la_SOURCES = \
	list.c \
	list.h \
	cbuf.c \
	cbuf.h \
	thread.h \
	hash.c \
	hash.h
