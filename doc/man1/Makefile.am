MAN1_FILES = \
	flux.1 \
	flux-broker.1 \
	flux-kvs.1 \
	flux-keygen.1 \
	flux-logger.1 \
	flux-up.1 \
	flux-topo.1 \
	flux-snoop.1 \
	flux-ping.1 \
	flux-wreckrun.1 \
	flux-start.1 \
	flux-config.1 \
	flux-module.1 \
	flux-exec.1 \
	flux-ps.1

ADOC_FILES  = $(MAN1_FILES:%.1=%.adoc)
XML_FILES   = $(MAN1_FILES:%.1=%.xml)

if HAVE_A2X
dist_man_MANS = $(MAN1_FILES)
$(MAN1_FILES): COPYRIGHT.adoc
endif

noinst_SCRIPTS = spellcheck

SUFFIXES = .adoc .1

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = 2>/dev/null

.adoc.1:
	$(AM_V_GEN)$(A2X) --attribute mansource=$(META_NAME) \
	    --attribute manversion=$(META_VERSION) \
	    --attribute manmanual="Flux Command Reference" \
	    --destination-dir=$(builddir) \
	    --doctype manpage --format manpage $< $(STDERR_DEVNULL)

spellcheck: $(top_srcdir)/doc/test/spellcheck
	$(LN_S) --force $< $@

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc

CLEANFILES = $(MAN1_FILES) $(XML_FILES) spellcheck

TESTS_ENVIRONMENT = \
	ASPELL=$(ASPELL) \
	pws_dict=$(abs_top_srcdir)/doc/test/spell.en.pws \
	man_dir=$(abs_srcdir)

TESTS = spellcheck
