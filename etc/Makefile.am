#if HAVE_SYSTEMD
systemdsystemunit_DATA = flux.service
#endif

tmpfilesdir = $(prefix)/lib/tmpfiles.d

tmpfiles_DATA = flux.conf

dist_fluxrc_SCRIPTS = \
        rc1 \
        rc3

dist_fluxrc1_SCRIPTS = \
        rc1.d/02-cron

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
    flux-hostlist.pc
endif

noinst_SCRIPTS = \
	completions/get_builtins.sh \
	completions/flux.pre

EXTRA_DIST = \
	gen-cmdhelp.py \
	flux.conf \
	$(noinst_SCRIPTS)

completions/flux: $(srcdir)/completions/flux.pre
	$(AM_V_GEN)test -d completions || mkdir completions && \
	cp $< $@ && chmod +w $@ && \
	$(srcdir)/completions/get_builtins.sh \
	$(top_srcdir)/src/cmd/builtin >> $@

fluxcompdir = $(sysconfdir)/bash_completion.d
fluxcomp_SCRIPTS = \
	completions/flux
