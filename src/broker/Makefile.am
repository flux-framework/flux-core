AM_CFLAGS = @GCCWARN@

libfluxdir = $(libdir)/flux
fluxmoddir = $(libfluxdir)/modules

AM_CPPFLAGS = \
	-DPLUGIN_PATH=\"$(top_builddir)/src/modules/api/.libs:$(top_builddir)/src/modules/kvs/.libs:$(top_builddir)/src/modules/modctl/.libs:$(fluxmoddir)\" \
	-DEXEC_PATH=\"$(top_builddir)/src/cmd:$(libexecdir)/flux\" \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux

bin_PROGRAMS = cmbd

cmbd_SOURCES = cmbd.c plugin.c pmi.c
cmbd_LDADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(LIBMUNGE) $(LIBJSON) $(LIBZMQ) $(LIBCZMQ) -ldl
