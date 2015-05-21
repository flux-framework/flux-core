AM_CFLAGS = @GCCWARN@

AM_CPPFLAGS = \
	$(JSON_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
        -DMODULE_PATH=\"$(fluxmoddir)\" \
        -DCONNECTOR_PATH=\"$(fluxconnectordir)\" \
	-DEXEC_PATH=\"$(fluxcmddir)\" \
	-DBROKER_PATH=\"$(fluxlibexecdir)/flux-broker\" \
	-DLUA_PATH_ADD=\"$(luadir)/?.lua\" \
	-DLUA_CPATH_ADD=\"$(luaexecdir)/?.so\" \
	-DMANDIR=\"$(mandir)\"

fluxcmd_ldadd = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/modules/live/liblive.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(JSON_LIBS) $(LIBZMQ) $(LIBCZMQ) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

LDADD = $(fluxcmd_ldadd)


bin_PROGRAMS = flux

#
# Flux subcommands
#

dist_fluxcmd_SCRIPTS = \
	flux-screen \
	flux-wreckrun \
	flux-exec \
	flux-topo

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
	flux-comms \
	flux-comms-stats \
	flux-kvs \
	flux-start \
	flux-config

flux_zio_SOURCES = \
	flux-zio.c \
	forkzio.c \
	forkzio.h
flux_zio_LDADD = \
	$(top_builddir)/src/modules/libzio/libzio.la \
	$(top_builddir)/src/modules/libkz/libkz.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
	
flux_mping_LDADD = \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
flux_module_LDADD = \
	$(top_builddir)/src/modules/modctl/libmodctl.la \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
