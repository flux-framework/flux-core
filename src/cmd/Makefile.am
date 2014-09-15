AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
        -DMODULE_PATH=\"$(fluxmoddir)\" -DEXEC_PATH=\"$(fluxbindir)\" \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/modules/live \
	-I$(top_srcdir)/src/modules/modctl

bin_PROGRAMS = flux

flux_SOURCES = flux.c
flux_LDADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(LIBZMQ)

#
# Flux subcommands
#

dist_fluxbin_SCRIPTS = \
	flux-srun \
	flux-screen

fluxbin_PROGRAMS = \
	flux-event \
	flux-info \
	flux-keygen \
	flux-kvs \
	flux-kvscopy \
	flux-kvsdir \
	flux-kvstorture \
	flux-kvswatch \
	flux-logger \
	flux-mod \
	flux-peer \
	flux-ping \
	flux-snoop \
	flux-stats \
	flux-up \
	flux-zio \
	flux-mping

fluxbin_ldadd = \
	$(top_builddir)/src/modules/live/liblive.la \
	$(top_builddir)/src/modules/modctl/libmodctl.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/api/libapi.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBMUNGE) $(LIBPTHREAD)

flux_event_SOURCES = flux-event.c
flux_event_LDADD = $(fluxbin_ldadd)

flux_info_SOURCES = flux-info.c
flux_info_LDADD = $(fluxbin_ldadd)

flux_keygen_SOURCES = flux-keygen.c
flux_keygen_LDADD = $(fluxbin_ldadd)

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

flux_logger_SOURCES = flux-logger.c
flux_logger_LDADD = $(fluxbin_ldadd)

flux_mod_SOURCES = flux-mod.c
flux_mod_LDADD = $(fluxbin_ldadd)

flux_peer_SOURCES = flux-peer.c
flux_peer_LDADD = $(fluxbin_ldadd)

flux_ping_SOURCES = flux-ping.c
flux_ping_LDADD = $(fluxbin_ldadd)

flux_snoop_SOURCES = flux-snoop.c
flux_snoop_LDADD = $(fluxbin_ldadd)

flux_stats_SOURCES = flux-stats.c
flux_stats_LDADD = $(fluxbin_ldadd)

flux_up_SOURCES = flux-up.c
flux_up_LDADD = $(fluxbin_ldadd)

flux_zio_SOURCES = flux-zio.c
flux_zio_LDADD = \
	$(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(fluxbin_ldadd) \
	$(LIBUTIL)
flux_zio_CPPFLAGS = \
	-I$(top_srcdir)/src/common/libzio \
	$(AM_CPPFLAGS)
flux_mping_SOURCES = flux-mping.c
flux_mping_LDADD = \
	$(top_builddir)/src/common/libmrpc/libmrpc.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(fluxbin_ldadd) \
	$(LIBUTIL)
flux_mping_CPPFLAGS = \
	-I$(top_srcdir)/src/common/libmrpc \
	$(AM_CPPFLAGS)
