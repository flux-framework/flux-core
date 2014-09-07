AM_CFLAGS = @GCCWARN@

fluxbindir = $(libexecdir)/flux
fluxlibdir = $(libdir)/flux
fluxmoddir = $(fluxlibdir)/modules

AM_CPPFLAGS = \
	-DPLUGIN_PATH=\"$(fluxmoddir)\" \
	-DEXEC_PATH=\"$(top_builddir)/src/cmd:$(fluxbindir)\" \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/lib/libflux \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/modules/modctl \
	-I$(top_srcdir)/src/modules/live

bin_PROGRAMS = flux

flux_SOURCES = flux.c
flux_LDADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(LIBZMQ)

#
# Flux subcommands
#

fluxbin_SCRIPTS = \
	srun \
	screen

fluxbin_PROGRAMS = \
	event \
	xinfo \
	keygen \
	kvs \
	kvscopy \
	kvsdir \
	kvstorture \
	kvswatch \
	logger \
	mod \
	peer \
	ping \
	snoop \
	stats \
	up

fluxbin_ldadd = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/lib/libflux/libflux.la \
	$(top_builddir)/src/modules/api/libflux-api.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(top_builddir)/src/modules/modctl/libflux-modctl.la \
	$(top_builddir)/src/modules/live/libflux-live.la \
	$(LIBJSON) $(LIBZMQ) $(LIBCZMQ) $(LIBMUNGE)

event_SOURCES = event.c
event_LDADD = $(fluxbin_ldadd)

xinfo_SOURCES = info.c
xinfo_LDADD = $(fluxbin_ldadd)

keygen_SOURCES = keygen.c
keygen_LDADD = $(fluxbin_ldadd)

kvs_SOURCES = kvs.c
kvs_LDADD = $(fluxbin_ldadd)

kvscopy_SOURCES = kvscopy.c
kvscopy_LDADD = $(fluxbin_ldadd)

kvsdir_SOURCES = kvsdir.c
kvsdir_LDADD = $(fluxbin_ldadd)

kvstorture_SOURCES = kvstorture.c
kvstorture_LDADD = $(fluxbin_ldadd)

kvswatch_SOURCES = kvswatch.c
kvswatch_LDADD = $(fluxbin_ldadd)

logger_SOURCES = logger.c
logger_LDADD = $(fluxbin_ldadd)

mod_SOURCES = mod.c
mod_LDADD = $(fluxbin_ldadd)

peer_SOURCES = peer.c
peer_LDADD = $(fluxbin_ldadd)

ping_SOURCES = ping.c
ping_LDADD = $(fluxbin_ldadd)

snoop_SOURCES = snoop.c
snoop_LDADD = $(fluxbin_ldadd)

stats_SOURCES = stats.c
stats_LDADD = $(fluxbin_ldadd)

up_SOURCES = up.c
up_LDADD = $(fluxbin_ldadd)
