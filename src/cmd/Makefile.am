AM_CPPFLAGS = \
	-DEXEC_PATH=\"$(top_builddir)/src/cmd:$(libexecdir)/flux\" \
	-I../common/libutil -I../lib/libflux

bin_PROGRAMS = flux

flux_SOURCES = flux.c
flux_LDADD = libflux.la $(LIBZMQ)

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
	flux-snoop \
	flux-stats \
	flux-up \
	flux-zio

fluxbin_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ)

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

flux_snoop_SOURCES = flux-snoop.c
flux_snoop_LDADD = $(fluxbin_ldadd) $(LIBMUNGE)

flux_stats_SOURCES = flux-stats.c
flux_stats_LDADD = $(fluxbin_ldadd)

flux_up_SOURCES = flux-up.c
flux_up_LDADD = $(fluxbin_ldadd)

flux_zio_SOURCES = flux-zio.c
flux_zio_LDADD = $(fluxbin_ldadd) $(LIBUTIL)
