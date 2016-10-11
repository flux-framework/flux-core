noinst_DATA = \
	flux/curve

dist_fluxrc_SCRIPTS = \
        rc1 \
        rc3

flux/curve:
	$(AM_V_GEN)$(top_builddir)/src/cmd/flux keygen --force

fluxhelpdir = $(datadir)/flux/help.d
fluxhelp_DATA = flux/help.d/core.json
flux/help.d/core.json: $(top_srcdir)/doc/man1
	$(AM_V_GEN)umask 077 && $(MKDIR_P) flux/help.d && \
	$(srcdir)/gen-cmdhelp.pl --category=core $^/*.adoc >$@

clean-local:
	-rm -rf flux

if WITH_PKG_CONFIG
pkgconfig_DATA = flux-core.pc
endif

EXTRA_DIST = \
	gen-cmdhelp.pl
