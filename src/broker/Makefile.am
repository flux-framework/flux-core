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
	modhandle.c \
	modhandle.h \
	modservice.c \
	modservice.h \
	boot_pmi.c \
	boot_pmi.h \
	endpt.h \
	endpt.c \
	overlay.h \
	overlay.c \
	heartbeat.h \
	heartbeat.c \
	snoop.h \
	snoop.c \
	service.h \
	service.c \
	hello.h \
	hello.c \
	shutdown.h \
	shutdown.c

flux_broker_LDADD = \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libev/libev.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	-L$(top_builddir)/src/modules/kvs -lkvs \
	-L$(top_builddir)/src/common -lflux-core \
	$(LIBMUNGE) $(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBPTHREAD) $(LIBDL)

flux_broker_LDFLAGS =
