AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-DMODULE_PATH=\"$(fluxmoddir)\" \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux

fluxbin_PROGRAMS = cmbd

cmbd_SOURCES = \
	cmbd.c \
	plugin.c \
	plugin.h \
	boot_pmi.c \
	boot_pmi.h

cmbd_LDADD = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD)
