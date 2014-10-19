noinst_DATA = \
	flux/config \
	flux/curve \
	flux-core-srctree.pc

flux/config: $(srcdir)/flux.conf.in
	$(AM_V_GEN)umask 077; $(MKDIR_P) flux; \
	sed -e 's![@]abs_top_srcdir[@]!$(abs_top_srcdir)!g' \
	    -e 's![@]abs_top_builddir[@]!$(abs_top_builddir)!g' \
	    $(srcdir)/flux.conf.in > $@

flux/curve: flux/config
	$(AM_V_GEN)$(top_builddir)/src/cmd/flux keygen

clean-local:
	-rm -rf flux

if WITH_PKG_CONFIG
pkgconfig_DATA = flux-core.pc
endif

ldconfdir=$(sysconfdir)/ld.so.conf.d
ldconf_DATA = flux-libs.conf

EXTRA_DIST = \
	flux.conf.in

