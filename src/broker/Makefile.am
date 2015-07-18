AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	-DMODULE_PATH=\"$(fluxmoddir)\"

fluxlibexec_PROGRAMS = flux-broker

flux_broker_SOURCES = \
	broker.c \
	module.c \
	module.h \
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
	$(top_builddir)/src/modules/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la 

broker_ldflags = 

if HAVE_PYTHON
#allow the broker to find the python library at runtime
broker_ldflags += -rpath $(PYTHON_PREFIX)/lib
endif

flux_broker_LDFLAGS = ${broker_ldflags}
