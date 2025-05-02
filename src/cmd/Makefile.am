AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS)

AM_CPPFLAGS = \
	$(CODE_COVERAGE_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux \
	-I$(top_srcdir)/src/common/libccan \
	$(ZMQ_CFLAGS) \
	$(FLUX_SECURITY_CFLAGS) \
	$(HWLOC_CFLAGS) \
	$(JANSSON_CFLAGS) \
	$(LIBSYSTEMD_CFLAGS) \
	$(LIBARCHIVE_CFLAGS)


fluxcmd_ldadd = \
	$(top_builddir)/src/common/libkvs/libkvs.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-optparse.la \
	$(FLUX_SECURITY_LIBS) \
	$(LIBPTHREAD) \
	$(JANSSON_LIBS)

LDADD = $(fluxcmd_ldadd)

EXTRA_DIST = \
	builtin-cmds-list.sh
CLEANFILES = \
	builtin-cmds.c \
	flux-pkill.py

BUILT_SOURCES = \
	builtin-cmds.c \
	flux-pkill.py

flux-pkill.py:
	$(AM_V_GEN)$(LN_S) $(srcdir)/flux-pgrep.py flux-pkill.py

install-exec-hook:
	$(AM_V_at)echo Linking flux-pkill to flux-pgrep && \
	 (cd $(DESTDIR)$(fluxcmddir) && \
	  rm -f flux-pkill.py && \
	  $(LN_S) flux-pgrep.py flux-pkill.py)

uninstall-local:
	$(AM_V_at)echo Removing $(fluxcmddir)/flux-pkill.py && \
	  rm -f $(fluxcmddir)/flux-pkill.py

bin_PROGRAMS = flux
flux_SOURCES = \
	flux.c \
	cmdhelp.h \
	cmdhelp.c \
	builtin.h \
	builtin/attr.c \
	builtin/config.c \
	builtin/help.c \
	builtin/dmesg.c \
	builtin/env.c \
	builtin/content.c \
	builtin/version.c \
	builtin/heaptrace.c \
	builtin/proxy.c \
	builtin/overlay.c \
	builtin/relay.c \
	builtin/python.c \
	builtin/uptime.c \
	builtin/startlog.c \
	builtin/dump.c \
	builtin/restore.c \
	builtin/archive.c \
	builtin/pmi.c \
	builtin/shutdown.c \
	builtin/lptest.c
nodist_flux_SOURCES = \
	builtin-cmds.c
dist_bin_SCRIPTS = flux-python

flux_LDADD = \
	$(top_builddir)/src/common/libpmi/libupmi.la \
	$(top_builddir)/src/common/libpmi/libpmi_client.la \
	$(top_builddir)/src/common/libpmi/libpmi_common.la \
	$(top_builddir)/src/common/libfilemap/libfilemap.la \
	$(LIBARCHIVE_LIBS) \
	$(LIBSYSTEMD_LIBS) \
	$(fluxcmd_ldadd)

#
# Flux subcommands
#

dist_fluxcmd_SCRIPTS = \
	flux-cron \
	flux-alloc.py \
	flux-batch.py \
	flux-run.py \
	flux-submit.py \
	flux-bulksubmit.py \
	flux-jobs.py \
	flux-fortune.py \
	flux-resource.py \
	flux-admin.py \
	flux-jobtap.py \
	flux-job-validator.py \
	flux-job-frobnicator.py \
	flux-job-exec-override.py \
	flux-uri.py \
	flux-pstree.py \
	flux-pgrep.py \
	flux-queue.py \
	flux-cancel.py \
	flux-watch.py \
	flux-update.py \
	flux-imp-exec-helper \
	py-runner.py \
	flux-hostlist.py \
	flux-post-job-event.py \
	flux-run-housekeeping \
	flux-housekeeping.py \
	flux-run-prolog \
	flux-run-epilog \
	flux-modprobe.py

fluxcmd_PROGRAMS = \
	flux-terminus \
	flux-ping \
	flux-keygen \
	flux-logger \
	flux-event \
	flux-module \
	flux-kvs \
	flux-start \
	flux-job \
	flux-exec \
	flux-R \
	flux-top

flux_job_SOURCES = \
	job/main.c \
	job/common.c \
	job/common.h \
	job/mpir.c \
	job/mpir.h \
	job/attach.c \
	job/submit.c \
	job/list.c \
	job/status.c \
	job/id.c \
	job/namespace.c \
	job/urgency.c \
	job/cancel.c \
	job/eventlog.c \
	job/info.c \
	job/stats.c \
	job/wait.c \
	job/memo.c \
	job/purge.c \
	job/taskmap.c \
	job/timeleft.c \
	job/last.c \
	job/hostpids.c

flux_start_LDADD = \
	$(fluxcmd_ldadd) \
	$(top_builddir)/src/common/libpmi/libpmi_server.la \
	$(top_builddir)/src/common/libpmi/libpmi_common.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la

flux_job_LDADD = \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(top_builddir)/src/shell/libmpir.la \
	$(top_builddir)/src/common/libjob/libjob.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libdebugged/libdebugged.la \
	$(top_builddir)/src/common/libterminus/libterminus.la \
	$(fluxcmd_ldadd) \
	$(top_builddir)/src/common/libflux/libflux.la

flux_exec_LDADD = \
	$(top_builddir)/src/common/libsubprocess/libsubprocess.la \
	$(fluxcmd_ldadd) \
	$(top_builddir)/src/common/libflux/libflux.la

flux_terminus_LDADD = \
	$(top_builddir)/src/common/libterminus/libterminus.la \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(fluxcmd_ldadd)

flux_R_LDADD = \
	$(fluxcmd_ldadd) \
	$(top_builddir)/src/common/librlist/librlist-hwloc.la \
	$(top_builddir)/src/common/librlist/librlist.la \
	$(top_builddir)/src/common/libczmqcontainers/libczmqcontainers.la \
	$(top_builddir)/src/common/libhostlist/libhostlist.la \
	$(top_builddir)/src/common/libidset/libidset.la \
	$(top_builddir)/src/common/libutil/libutil.la \
	$(HWLOC_LIBS) \
	$(JANSSON_LIBS)

flux_keygen_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(ZMQ_CFLAGS)

flux_keygen_LDADD = \
	$(top_builddir)/src/common/libzmqutil/libzmqutil.la \
	$(fluxcmd_ldadd) \
	$(ZMQ_LIBS)

flux_top_SOURCES = \
	top/top.c \
	top/top.h \
	top/keys.c \
	top/joblist_pane.c \
	top/summary_pane.c \
	top/ucache.c \
	top/queues.c
flux_top_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(CURSES_CFLAGS)
flux_top_LDADD = \
	$(fluxcmd_ldadd) \
	$(CURSES_LIBS)

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
