AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
	-I$(top_srcdir) -I$(top_srcdir)/src/include \
	$(ZMQ_CFLAGS)

fluxcmd_ldadd = \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(top_builddir)/src/modules/kvs/libflux-kvs.la \
	$(ZMQ_LIBS) $(LIBMUNGE) $(LIBPTHREAD) $(LIBDL)

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
	builtin/nodeset.c \
	builtin/heaptrace.c \
	builtin/proxy.c \
	builtin/user.c
nodist_flux_SOURCES = \
	builtin-cmds.c

#
# Flux subcommands
#

dist_fluxcmd_SCRIPTS = \
	flux-submit \
	flux-wreckrun \
	flux-wreck \
	flux-exec \
	flux-ps \
	flux-cron \
	flux-aggregate

if HAVE_PYTHON
dist_fluxcmd_SCRIPTS+= \
 flux-list-instances
endif

fluxcmd_PROGRAMS = \
	flux-ping \
	flux-keygen \
	flux-logger \
	flux-event \
	flux-module \
	flux-comms \
	flux-kvs \
	flux-start \
	flux-jstat

flux_start_LDADD = \
	$(fluxcmd_ldadd) \
	$(LIBUTIL)
flux_jstat_LDADD = \
	$(top_builddir)/src/modules/libjsc/libflux-jsc.la \
	$(fluxcmd_ldadd) \
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
