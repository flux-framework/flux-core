MAN1 = 	flux.1 \
	flux-kvs.1 \
	flux-keygen.1 \
	flux-logger.1 \
	flux-up.1 \
	flux-topo.1 \
	flux-snoop.1

ADOC_FILES  = $(MAN1:%.1=%.adoc)
XML_FILES   = $(MAN1:%.1=%.xml)

dist_man_MANS = $(MAN1)

$(MAN1): COPYRIGHT.adoc

SUFFIXES = .adoc .1

.adoc.1:
	a2x --attribute mansource=$(META_NAME) \
	    --attribute manversion=$(META_VERSION) \
	    --attribute manmanual="Flux Manual" \
	    --doctype manpage --format manpage $<

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc

CLEANFILES = $(MAN1) $(XML_FILES)
