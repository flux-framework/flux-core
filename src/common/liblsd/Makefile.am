AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	-Wno-parentheses -Wno-error=parentheses \
	-Wno-error=unused-but-set-variable \
	-Wno-unused-but-set-variable

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-DNDEBUG

noinst_LTLIBRARIES = liblsd.la

liblsd_la_SOURCES = \
	cbuf.c \
	cbuf.h
