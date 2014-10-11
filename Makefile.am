SUBDIRS = src doc t

sysconf_DATA = etc/flux/config

etc/flux/config: $(srcdir)/flux.conf.in
	$(AM_V_GEN)umask 077; $(MKDIR_P) etc/flux; \
	sed -e 's![@]abs_top_srcdir[@]!$(abs_top_srcdir)!g' \
	    -e 's![@]abs_top_builddir[@]!$(abs_top_builddir)!g' \
	    $(srcdir)/flux.conf.in > $@

EXTRA_DIST = \
	META \
	DISCLAIMER.LLNS \
	README.md \
	flux.conf.in

clean-local:
	-rm -rf etc
