SUBDIRS = util zutil . 

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-DPLUGIN_PATH=\"$(top_builddir)/zmq-broker/.libs\" \
	-DEXEC_PATH=\"$(top_builddir)/zmq-broker\" \
	-Iutil -Izutil -Iinclude

noinst_LTLIBRARIES = \
	libcmb.la

libcmb_la_SOURCES = \
	apicli.c \
	kvscli.c \
	barriercli.c \
	logcli.c \
	cmbdcli.c \
	mrpc.c \
	handle.c \
        waitqueue.c \
	security.c \
	reduce.c \
	modctlcli.c \
        livecli.c
libcmb_la_LIBADD = \
	$(top_builddir)/zmq-broker/util/libutil.la \
	$(LIBMUNGE) $(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

bin_PROGRAMS = \
	cmbd \
	flux

cmbd_SOURCES = \
	cmbd.c \
	plugin.c \
	pmiwrap.c
cmbd_LDADD = \
	$(top_builddir)/zmq-broker/util/libutil.la \
	libcmb.la
	$(LIBMUNGE) $(LIBJSON) $(LIBZMQ) $(LIBCZMQ) -ldl

flux_SOURCES = \
	flux.c
flux_LDADD = \
	$(top_builddir)/zmq-broker/util/libutil.la \
	libcmb.la \
	$(LIBMUNGE) $(LIBJSON) $(LIBZMQ) $(LIBCZMQ) -ldl

#
# commands
#

fluxbindir = $(bindir)/flux
fluxbin_PROGRAMS = \
	flux-info \
	flux-up

fluxbin_ldadd = \
	$(top_builddir)/zmq-broker/zutil/libzutil.la \
	$(top_builddir)/zmq-broker/util/libutil.la \
	libcmb.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

flux_info_SOURCES = flux-info.c
flux_info_LDADD = $(fluxbin_ldadd)

flux_up_SOURCES = flux-up.c
flux_up_LDADD = $(fluxbin_ldadd)

#
# comms modules
#

fluxmoddir = $(libdir)/flux-modules
fluxmod_LTLIBRARIES = \
	kvs.la \
	api.la \
	modctl.la

fluxmod_libadd = \
	libcmb.la

fluxmod_ldflags = -module -avoid-version -Wl,--version-script=module.map

kvs_la_SOURCES = kvssrv.c
kvs_la_LDFLAGS = $(fluxmod_ldflags)
kvs_la_LIBADD = $(fluxmod_libadd)

api_la_SOURCES = apisrv.c
api_la_LDFLAGS = $(fluxmod_ldflags)
api_la_LIBADD = $(fluxmod_libadd)

modctl_la_SOURCES = modctlsrv.c
modctl_la_LDFLAGS = $(fluxmod_ldflags)
modctl_la_LIBADD = $(fluxmod_libadd)
