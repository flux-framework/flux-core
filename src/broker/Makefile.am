AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-DMODULE_PATH=\"$(fluxmoddir)\"

fluxlibexec_PROGRAMS = flux-broker

flux_broker_SOURCES = \
	broker.c \
	module.c \
	module.h \
	boot_pmi.c \
	boot_pmi.h \
	endpt.h \
	endpt.c \
	peer.h \
	peer.c \
	overlay.h \
	overlay.c \
	heartbeat.h \
	heartbeat.c \
	snoop.h \
	snoop.c \
	service.h \
	service.c

flux_broker_LDADD = \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/api/libapi.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD) $(LIBDL)
