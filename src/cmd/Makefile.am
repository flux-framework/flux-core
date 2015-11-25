AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	$(JSON_CFLAGS) $(ZMQ_CFLAGS) \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
        -DMODULE_PATH=\"$(fluxmoddir)\" \
        -DCONNECTOR_PATH=\"$(fluxconnectordir)\" \
	-DEXEC_PATH=\"$(fluxcmddir)\" \
	-DBROKER_PATH=\"$(fluxlibexecdir)/flux-broker\" \
	-DLUA_PATH_ADD=\"$(luadir)/?.lua\" \
	-DLUA_CPATH_ADD=\"$(luaexecdir)/?.so\" \
	-DPYTHON_PATH=\"$(pyexecdir)\" \
	-DMANDIR=\"$(mandir)\"

fluxcmd_ldadd = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/modules/kvs/libkvs.la \
	$(top_builddir)/src/modules/live/liblive.la \
	$(JSON_LIBS) $(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

LDADD = $(fluxcmd_ldadd)


bin_PROGRAMS = flux

#
# Flux subcommands
#

dist_fluxcmd_SCRIPTS = \
	flux-submit \
	flux-wreckrun \
	flux-wreck \
	flux-exec \
	flux-topo \
	flux-lstopo \
	flux-ps

if HAVE_PYTHON
dist_fluxcmd_SCRIPTS+= \
 flux-list-instances
endif

fluxcmd_PROGRAMS = \
	flux-ping \
	flux-mping \
	flux-up \
	flux-keygen \
	flux-logger \
	flux-snoop \
	flux-event \
	flux-module \
	flux-comms \
	flux-comms-stats \
	flux-kvs \
	flux-start \
	flux-config \
	flux-jstat

flux_start_LDADD = \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
flux_mping_LDADD = \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
flux_module_LDADD = \
	$(top_builddir)/src/modules/modctl/libmodctl.la \
	$(top_builddir)/src/modules/libmrpc/libmrpc.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
flux_jstat_LDADD = \
	$(top_builddir)/src/modules/libjsc/libjsc.la \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)

#
# coverage:
#  Ensure all programs are run at least once, so we can see which
#  have zero coverage:
#
cmd-run-test: $(fluxcmd_PROGRAMS)
	@for f in $(fluxcmd_PROGRAMS); do ./$$f --help 2>/dev/null; done
