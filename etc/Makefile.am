noinst_DATA = \
	flux/config \
	flux/curve

flux/config: $(srcdir)/flux.conf.in
	$(AM_V_GEN)umask 077; $(MKDIR_P) flux; \
	sed -e 's![@]abs_top_srcdir[@]!$(abs_top_srcdir)!g' \
	    -e 's![@]abs_top_builddir[@]!$(abs_top_builddir)!g' \
	    $(srcdir)/flux.conf.in > $@

flux/curve: flux/config
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
	flux.conf.in \
	Makefile.inc.in \
	gen-cmdhelp.pl
