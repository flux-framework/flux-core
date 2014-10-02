SUBDIRS = src doc

sysconf_DATA = flux.conf

flux.conf: $(srcdir)/flux.conf.in
	$(AM_V_GEN)sed -e 's![@]abs_top_srcdir[@]!$(abs_top_srcdir)!g' \
	    -e 's![@]abs_top_builddir[@]!$(abs_top_builddir)!g' \
	    $(srcdir)/flux.conf.in > $@

EXTRA_DIST = \
	META \
	DISCLAIMER.LLNS \
	README.md \
	flux.conf.in

CLEANFILES = flux.conf

