ADOC_FILES = \
	flux.adoc \
	flux-kvs.adoc \
	flux-keygen.adoc \
	flux-logger.adoc \
	flux-up.adoc \
	flux-topo.adoc \
	flux-snoop.adoc \
	flux-ping.adoc

MAN1_FILES  = $(ADOC_FILES:%.adoc=%.1)
XML_FILES   = $(ADOC_FILES:%.adoc=%.xml)

dist_man_MANS = $(MAN1_FILES)

$(MAN1): COPYRIGHT.adoc

SUFFIXES = .adoc .1

.adoc.1:
	a2x --attribute mansource=$(META_NAME) \
	    --attribute manversion=$(META_VERSION) \
	    --attribute manmanual="Flux Manual" \
	    --doctype manpage --format manpage $<

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc spellcheck spell.en.pws

CLEANFILES = $(MAN1_FILES) $(XML_FILES)

TESTS_ENVIRONMENT = ./spellcheck

TESTS = $(ADOC_FILES)
