AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-DPLUGIN_PATH=\"$(top_builddir)/src/plugins/.libs\" \
	-DEXEC_PATH=\"$(top_builddir)/src/cmd:$(libexecdir)/flux\" \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux

bin_PROGRAMS = cmbd

cmbd_SOURCES = cmbd.c plugin.c pmi.c
cmbd_LDADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(LIBMUNGE) $(LIBJSON) $(LIBZMQ) $(LIBCZMQ) -ldl
