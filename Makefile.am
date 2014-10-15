SUBDIRS = src doc t

sysconf_DATA = \
	etc/flux/config
noinst_DATA = \
	etc/flux/curve

etc/flux/config: $(srcdir)/flux.conf.in
	$(AM_V_GEN)umask 077; $(MKDIR_P) etc/flux; \
	sed -e 's![@]abs_top_srcdir[@]!$(abs_top_srcdir)!g' \
	    -e 's![@]abs_top_builddir[@]!$(abs_top_builddir)!g' \
	    $(srcdir)/flux.conf.in > $@

etc/flux/curve: etc/flux/config
	$(AM_V_GEN)$(top_builddir)/src/cmd/flux keygen

EXTRA_DIST = \
	config/tap-driver.sh \
	META \
	DISCLAIMER.LLNS \
	README.md \
	flux.conf.in

clean-local:
	-rm -rf etc
