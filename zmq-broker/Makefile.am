SUBDIRS = util zutil .

AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	-DPLUGIN_PATH=\"$(top_builddir)/zmq-broker/.libs\" \
	-DEXEC_PATH=\"$(top_builddir)/zmq-broker:$(libexecdir)/flux\" \
	-Iutil -Izutil -Iinclude

noinst_LTLIBRARIES = \
	libflux.la

libflux_la_SOURCES = \
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
        livecli.c \
	rdl.c \
	dlua/json-lua.c
libflux_la_LIBADD = \
	$(top_builddir)/zmq-broker/util/libutil.la \
	$(top_builddir)/zmq-broker/zutil/libzutil.la

#
# programs
#

bin_PROGRAMS = cmbd flux

cmbd_SOURCES = cmbd.c plugin.c pmiwrap.c
cmbd_LDADD = libflux.la $(LIBMUNGE) $(LIBJSON) $(LIBZMQ) $(LIBCZMQ) -ldl

flux_SOURCES = flux.c
flux_LDADD = libflux.la $(LIBZMQ)

#
# flux sub-commands
#

fluxbindir = $(libexecdir)/flux
fluxbin_SCRIPTS = \
	flux-topo \
	flux-srun \
	flux-screen \
	flux-submit \
	flux-wreckrun

fluxbin_PROGRAMS = \
	flux-barrier \
	flux-event \
	flux-host \
	flux-info \
	flux-keygen \
	flux-kvs \
	flux-kvscopy \
	flux-kvsdir \
	flux-kvstorture \
	flux-kvswatch \
	flux-log \
	flux-logger \
	flux-mecho \
	flux-mod \
	flux-mon \
	flux-peer \
	flux-ping \
	flux-rdltool \
	flux-snoop \
	flux-stats \
	flux-up \
	flux-zio

fluxbin_ldadd = libflux.la $(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

flux_barrier_SOURCES = flux-barrier.c
flux_barrier_LDADD = $(fluxbin_ldadd)

flux_event_SOURCES = flux-event.c
flux_event_LDADD = $(fluxbin_ldadd)

flux_host_SOURCES = flux-host.c
flux_host_LDADD = $(fluxbin_ldadd)

flux_info_SOURCES = flux-info.c
flux_info_LDADD = $(fluxbin_ldadd)

flux_keygen_SOURCES = flux-keygen.c
flux_keygen_LDADD = $(fluxbin_ldadd) $(LIBMUNGE)

flux_kvs_SOURCES = flux-kvs.c
flux_kvs_LDADD = $(fluxbin_ldadd)

flux_kvscopy_SOURCES = flux-kvscopy.c
flux_kvscopy_LDADD = $(fluxbin_ldadd)

flux_kvsdir_SOURCES = flux-kvsdir.c
flux_kvsdir_LDADD = $(fluxbin_ldadd)

flux_kvstorture_SOURCES = flux-kvstorture.c
flux_kvstorture_LDADD = $(fluxbin_ldadd)

flux_kvswatch_SOURCES = flux-kvswatch.c
flux_kvswatch_LDADD = $(fluxbin_ldadd)

flux_log_SOURCES = flux-log.c
flux_log_LDADD = $(fluxbin_ldadd)

flux_logger_SOURCES = flux-logger.c
flux_logger_LDADD = $(fluxbin_ldadd)

flux_mecho_SOURCES = flux-mecho.c
flux_mecho_LDADD = $(fluxbin_ldadd)

flux_mod_SOURCES = flux-mod.c
flux_mod_LDADD = $(fluxbin_ldadd)

flux_mon_SOURCES = flux-mon.c
flux_mon_LDADD = $(fluxbin_ldadd)

flux_peer_SOURCES = flux-peer.c
flux_peer_LDADD = $(fluxbin_ldadd)

flux_ping_SOURCES = flux-ping.c
flux_ping_LDADD = $(fluxbin_ldadd)

flux_rdltool_SOURCES = flux-rdltool.c
flux_rdltool_LDADD = $(fluxbin_ldadd) $(LIBLUA)

flux_snoop_SOURCES = flux-snoop.c
flux_snoop_LDADD = $(fluxbin_ldadd) $(LIBMUNGE)

flux_stats_SOURCES = flux-stats.c
flux_stats_LDADD = $(fluxbin_ldadd)

flux_up_SOURCES = flux-up.c
flux_up_LDADD = $(fluxbin_ldadd)

flux_zio_SOURCES = flux-zio.c
flux_zio_LDADD = $(fluxbin_ldadd) $(LIBUTIL)

#
# comms modules
#

fluxmoddir = $(libdir)/flux-modules
fluxmod_LTLIBRARIES = \
	kvs.la \
	api.la \
	modctl.la \
	barrier.la \
	echo.la \
	live.la \
	mecho.la \
	mon.la

fluxmod_libadd = libflux.la
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

barrier_la_SOURCES = barriersrv.c
barrier_la_LDFLAGS = $(fluxmod_ldflags)
barrier_la_LIBADD = $(fluxmod_libadd)

echo_la_SOURCES = echosrv.c
echo_la_LDFLAGS = $(fluxmod_ldflags)
echo_la_LIBADD = $(fluxmod_libadd)

live_la_SOURCES = livesrv.c
live_la_LDFLAGS = $(fluxmod_ldflags)
live_la_LIBADD = $(fluxmod_libadd)

mecho_la_SOURCES = mechosrv.c
mecho_la_LDFLAGS = $(fluxmod_ldflags)
mecho_la_LIBADD = $(fluxmod_libadd)

mon_la_SOURCES = monsrv.c
mon_la_LDFLAGS = $(fluxmod_ldflags)
mon_la_LIBADD = $(fluxmod_libadd)
