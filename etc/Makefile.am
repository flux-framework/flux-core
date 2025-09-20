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

nobase_dist_fluxlibexec_SCRIPTS = \
	modprobe/rc1.py \
	modprobe/rc3.py

nobase_dist_fluxdata_DATA = \
	modprobe/modprobe.toml

fluxlibexec_SCRIPTS = \
	flux-run-system-scripts

# Install empty modprobe/*.d directories after rc1.py and rc3.py are installed
# Install empty fluxlibexecdir/{prolog,epilog,housekeeping}.d directories
install-data-hook:
	for dir in $(fluxconfdir) $(fluxdatadir); do \
	  $(MKDIR_P) $(DESTDIR)$$dir/modprobe/modprobe.d; \
	done
	for dir in $(fluxconfdir) $(fluxlibexecdir); do \
	  $(MKDIR_P) $(DESTDIR)$$dir/modprobe/rc1.d; \
	  $(MKDIR_P) $(DESTDIR)$$dir/modprobe/rc3.d; \
	done
	for dir in prolog.d epilog.d housekeeping.d; do \
	  $(MKDIR_P) $(DESTDIR)$(fluxlibexecdir)/$$dir; \
	done

# Remove empty modprobe/{prolog,epilog,housekeeping}.d directories on uninstall
uninstall-hook:
	for dir in $(fluxconfdir) $(fluxdatadir); do \
	  rmdir $(DESTDIR)$$dir/modprobe/modprobe.d 2>/dev/null || :; \
	done
	for dir in $(fluxconfdir) $(fluxlibexecdir); do \
	  rmdir $(DESTDIR)$$dir/modprobe/rc1.d 2>/dev/null || :; \
	  rmdir $(DESTDIR)$$dir/modprobe/rc3.d 2>/dev/null || :; \
	done
	for dir in $(fluxconfdir) $(fluxlibexecdir) $(fluxdatadir); do \
	  rmdir $(DESTDIR)$$dir/modprobe 2>/dev/null || :; \
	done
	for dir in prolog.d epilog.d housekeeping.d; do \
	  rmdir $(DESTDIR)$(fluxlibexecdir)/$$dir 2>/dev/null || :; \
	done

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
