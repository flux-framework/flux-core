MAN1_FILES = \
	flux.1 \
	flux-kvs.1 \
	flux-keygen.1 \
	flux-logger.1 \
	flux-up.1 \
	flux-topo.1 \
	flux-snoop.1 \
	flux-ping.1

ADOC_FILES  = $(MAN1_FILES:%.1=%.adoc)
XML_FILES   = $(MAN1_FILES:%.1=%.xml)

dist_man_MANS = $(MAN1_FILES)

$(MAN1_FILES): COPYRIGHT.adoc

SUFFIXES = .adoc .1

.adoc.1:
	a2x --attribute mansource=$(META_NAME) \
	    --attribute manversion=$(META_VERSION) \
	    --attribute manmanual="Flux Manual" \
	    --doctype manpage --format manpage $<

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc spellcheck spell.en.pws

CLEANFILES = $(MAN1_FILES) $(XML_FILES)

TESTS_ENVIRONMENT = $(srcdir)/spellcheck $(abs_srcdir)/spell.en.pws

TESTS = $(MAN1_FILES)
