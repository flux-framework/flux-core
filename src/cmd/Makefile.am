AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
        -DMODULE_PATH=\"$(fluxmoddir)\" \
	-DEXEC_PATH=\"$(fluxcmddir)\" \
	-DCMBD_PATH=\"$(fluxlibexecdir)/cmbd\" \
	-I$(top_srcdir)/src/common/libutil \
	-I$(top_srcdir)/src/common/libflux \
	-I$(top_srcdir)/src/common/liblsd \
	-I$(top_srcdir)/src/modules/api \
	-I$(top_srcdir)/src/modules/kvs \
	-I$(top_srcdir)/src/modules/live \
	-I$(top_srcdir)/src/modules/modctl \
	-I$(top_srcdir)/src/common/libzio \
	-I$(top_srcdir)/src/common/libmrpc

fluxcmd_ldadd = \
	$(top_builddir)/src/modules/live/liblive.la \
	$(top_builddir)/src/modules/modctl/libmodctl.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/api/libapi.la \
	$(top_builddir)/src/common/libflux/libflux.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBMUNGE) $(LIBPTHREAD)

LDADD = $(fluxcmd_ldadd)


bin_PROGRAMS = flux

flux_SOURCES = flux.c
flux_LDADD = \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(LIBZMQ)

#
# Flux subcommands
#

dist_fluxcmd_SCRIPTS = \
	flux-start-srun \
	flux-start-screen \
	flux-start-single

fluxcmd_PROGRAMS = \
	flux-ping \
	flux-mping \
	flux-up \
	flux-keygen \
	flux-logger \
	flux-snoop \
	flux-event \
	flux-zio \
	flux-module \
	flux-info \
	flux-peer \
	flux-stats \
	flux-kvs \
	flux-kvstorture

flux_zio_LDADD = \
	$(top_builddir)/src/common/libzio/libzio.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
flux_mping_LDADD = \
	$(top_builddir)/src/common/libmrpc/libmrpc.la \
	$(top_builddir)/src/common/liblsd/liblsd.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
