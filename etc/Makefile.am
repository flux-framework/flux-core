noinst_DATA = \
	flux/config \
	flux/curve

flux/config: $(srcdir)/flux.conf.in
	$(AM_V_GEN)umask 077; $(MKDIR_P) flux; \
	sed -e 's![@]abs_top_srcdir[@]!$(abs_top_srcdir)!g' \
	    -e 's![@]abs_top_builddir[@]!$(abs_top_builddir)!g' \
	    $(srcdir)/flux.conf.in > $@

flux/curve: flux/config
	$(AM_V_GEN)$(top_builddir)/src/cmd/flux keygen

EXTRA_DIST = \
	flux.conf.in

clean-local:
	-rm -rf flux
