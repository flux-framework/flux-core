#if HAVE_SYSTEMD
systemdsystemunit_DATA = \
	flux.service \
	flux-housekeeping@.service \
	flux-prolog@.service \
	flux-epilog@.service
#endif

tmpfilesdir = $(prefix)/lib/tmpfiles.d
crondir = $(fluxconfdir)/system/cron.d
fluxrc1dir = $(fluxconfdir)/rc1.d

tmpfiles_DATA = flux.conf

dist_cron_DATA = \
	kvs-backup.cron

nobase_dist_fluxconf_SCRIPTS = \
	rc1 \
	rc1.old \
	rc3 \
	rc3.old \
	modprobe/rc1.py \
	modprobe/rc3.py

nobase_dist_fluxconf_DATA = \
	modprobe/modprobe.toml

fluxhelpdir = $(datadir)/flux/help.d
fluxhelp_DATA = flux/help.d/core.json
flux/help.d/core.json: $(top_srcdir)/doc/manpages.py
	$(AM_V_GEN)umask 077 && $(MKDIR_P) flux/help.d && \
	$(PYTHON) $(srcdir)/gen-cmdhelp.py $< > $@

if !ENABLE_DOCS
fluxnodocsdir = $(datadir)/flux/.nodocs
fluxnodocs_DATA = flux/.nodocs
flux/.nodocs:
	$(AM_V_GEN)umask 077 && $(MKDIR_P) flux && \
	touch flux/.nodocs
endif

clean-local:
	-rm -rf flux

CLEANFILES=completions/flux

if WITH_PKG_CONFIG
pkgconfig_DATA = flux-core.pc \
	flux-pmi.pc \
	flux-optparse.pc \
	flux-idset.pc \
	flux-schedutil.pc \
	flux-hostlist.pc \
	flux-taskmap.pc
endif

noinst_SCRIPTS = \
	completions/get_builtins.sh \
	completions/flux.pre

EXTRA_DIST = \
	gen-cmdhelp.py \
	flux.conf \
	$(noinst_SCRIPTS)

completions/flux: completions/flux.pre completions/get_builtins.sh
	$(AM_V_GEN)test -d completions || mkdir completions && \
	cp $< $@ && chmod +w $@ && \
	$(srcdir)/completions/get_builtins.sh \
	$(top_srcdir)/src/cmd/builtin >> $@

bashcomp_SCRIPTS = \
	completions/flux
