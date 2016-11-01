MAN7_FILES = \
	flux-broker-attributes.7

ADOC_FILES  = $(MAN7_FILES:%.7=%.adoc)
XML_FILES   = $(MAN7_FILES:%.7=%.xml)

if ENABLE_DOCS
dist_man_MANS = $(MAN7_FILES)
$(MAN7_FILES): COPYRIGHT.adoc
endif

SUFFIXES = .adoc .7

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = 2>/dev/null

.adoc.7:
	$(AM_V_GEN)$(A2X) --attribute mansource=$(PACKAGE_NAME) \
	    --attribute manversion=$(PACKAGE_VERSION) \
	    --attribute manmanual="Flux Command Reference" \
	    --destination-dir=$(builddir) \
	    --doctype manpage --format manpage $< $(STDERR_DEVNULL)

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc

CLEANFILES = $(MAN7_FILES) $(XML_FILES)
