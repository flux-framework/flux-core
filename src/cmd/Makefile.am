AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	$(ZMQ_CFLAGS) $(FLUX_SECURITY_CFLAGS) $(LIBSODIUM_CFLAGS)

fluxcmd_ldadd = \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/libaggregate/libaggregate.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(FLUX_SECURITY_LIBS) \
	$(ZMQ_LIBS) $(LIBPTHREAD) $(LIBDL) $(HWLOC_LIBS) $(LIBSODIUM_LIBS)

LDADD = $(fluxcmd_ldadd)

EXTRA_DIST = \
	builtin-cmds-list.sh
CLEANFILES = \
	builtin-cmds.c
BUILT_SOURCES = \
	builtin-cmds.c

bin_PROGRAMS = flux
flux_SOURCES = \
	flux.c \
	cmdhelp.h \
	cmdhelp.c \
	builtin.h \
	builtin/attr.c \
	builtin/help.c \
	builtin/dmesg.c \
	builtin/env.c \
	builtin/content.c \
	builtin/version.c \
	builtin/hwloc.c \
	builtin/heaptrace.c \
	builtin/proxy.c \
	builtin/python.c \
	builtin/user.c
nodist_flux_SOURCES = \
	builtin-cmds.c

#
# Flux subcommands
#

dist_fluxcmd_SCRIPTS = \
	flux-cron \
	flux-hostlist

fluxcmd_PROGRAMS = \
	flux-aggregate \
	flux-ping \
	flux-keygen \
	flux-logger \
	flux-event \
	flux-module \
	flux-comms \
	flux-kvs \
	flux-start \
	flux-job \
	flux-exec

flux_start_LDADD = \
	$(fluxcmd_ldadd) \
	$(top_builddir)/src/common/libpmi/libpmi.la \
	$(LIBUTIL)

#
# Automatically build list of flux(1) builtins from
#  builtin/*.c:
builtin-cmds.c : builtin builtin-cmds-list.sh
	$(AM_V_GEN)$(SHELL) \
	    $(srcdir)/builtin-cmds-list.sh $(srcdir)/builtin/*.c > $@

#
# coverage:
#  Ensure all programs are run at least once, so we can see which
#  have zero coverage:
#
cmd-run-test: $(fluxcmd_PROGRAMS)
	@for f in $(fluxcmd_PROGRAMS); do ./$$f --help 2>/dev/null; done
