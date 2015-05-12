AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include

noinst_LTLIBRARIES = \
	libzio.la

libzio_la_SOURCES = \
	zio.c \
	forkzio.c \
	kz.c \
	forkzio.h \
	kz.h \
	zio.h

check_PROGRAMS = \
	zio-test \
	flux-zio-test

libzio_deps = \
	$(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libev/libev.la

LDADD = $(libzio_deps) \
	$(JSON_LIBS) $(LIBCZMQ) $(LIBZMQ) $(LIBMUNGE) \
	$(LIBPTHREAD) $(LIBDL)

$(libzio_deps):
	@(cd `dirname $@` && $(MAKE) `basename $@`)
