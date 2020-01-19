MAN5_FILES = \
	flux-config-bootstrap.5

ADOC_FILES  = $(MAN5_FILES:%.5=%.adoc)
XML_FILES   = $(MAN5_FILES:%.5=%.xml)

if ENABLE_DOCS
dist_man_MANS = $(MAN5_FILES)
$(MAN5_FILES): COPYRIGHT.adoc
endif

SUFFIXES = .adoc .5

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = 2>/dev/null

.adoc.5:
	$(AM_V_GEN)$(ADOC) --attribute mansource=$(PACKAGE_NAME) \
	    --attribute manversion=$(PACKAGE_VERSION) \
	    --attribute manmanual="Flux Miscellaneous Reference" \
	    --destination-dir $(builddir) \
	    --doctype manpage $(ADOC_FORMAT_OPT) manpage $< $(STDERR_DEVNULL)

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc

CLEANFILES = $(MAN5_FILES) $(XML_FILES)
