AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-DMODULE_PATH=\"$(fluxmoddir)\"

fluxlibexec_PROGRAMS = cmbd

cmbd_SOURCES = \
	cmbd.c \
	module.c \
	module.h \
	boot_pmi.c \
	boot_pmi.h

cmbd_LDADD = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/api/libapi.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD) $(LIBDL)
