MAN1_FILES = \
	flux.1 \
	cmbd.1 \
	flux-kvs.1 \
	flux-keygen.1 \
	flux-logger.1 \
	flux-up.1 \
	flux-topo.1 \
	flux-snoop.1 \
	flux-ping.1 \
	flux-wreckrun.1 \
	flux-start.1 \
	flux-config.1

ADOC_FILES  = $(MAN1_FILES:%.1=%.adoc)
XML_FILES   = $(MAN1_FILES:%.1=%.xml)

dist_man_MANS = $(MAN1_FILES)

$(MAN1_FILES): COPYRIGHT.adoc

SUFFIXES = .adoc .1

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = 2>/dev/null

.adoc.1:
	$(AM_V_GEN)$(A2X) --attribute mansource=$(META_NAME) \
	    --attribute manversion=$(META_VERSION) \
	    --attribute manmanual="Flux Manual" \
	    --destination-dir=$(builddir) \
	    --doctype manpage --format manpage $< $(STDERR_DEVNULL)

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc spell.en.pws

CLEANFILES = $(MAN1_FILES) $(XML_FILES)

dist_noinst_SCRIPTS = spellcheck

TESTS_ENVIRONMENT = \
	ASPELL=$(ASPELL) \
	pws_dict=$(abs_srcdir)/spell.en.pws \
	man_dir=$(abs_builddir)

TESTS = spellcheck
