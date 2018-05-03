MAN1_FILES = $(MAN1_FILES_PRIMARY) $(MAN1_FILES_SECONDARY)


MAN1_FILES_PRIMARY = \
	flux.1 \
	flux-broker.1 \
	flux-kvs.1 \
	flux-keygen.1 \
	flux-logger.1 \
	flux-ping.1 \
	flux-submit.1 \
	flux-wreckrun.1 \
	flux-wreck.1 \
	flux-start.1 \
	flux-module.1 \
	flux-exec.1 \
	flux-env.1 \
	flux-ps.1 \
	flux-getattr.1 \
	flux-dmesg.1 \
	flux-content.1 \
	flux-hwloc.1 \
	flux-proxy.1 \
	flux-cron.1 \
	flux-user.1 \
	flux-event.1 \
	flux-hostlist.1

# These files are generated as roff .so includes of a primary page.
# A2X handles this automatically if mentioned in NAME section
MAN1_FILES_SECONDARY = \
	flux-setattr.1 \
	flux-lsattr.1


ADOC_FILES  = $(MAN1_FILES_PRIMARY:%.1=%.adoc)
XML_FILES   = $(MAN1_FILES_PRIMARY:%.1=%.xml)

if ENABLE_DOCS
dist_man_MANS = $(MAN1_FILES)
$(MAN1_FILES): COPYRIGHT.adoc NODESET.adoc
endif

SUFFIXES = .adoc .1

flux-setattr.1: flux-getattr.1
flux-lsattr.1: flux-getattr.1

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = 2>/dev/null

.adoc.1:
	$(AM_V_GEN)$(A2X) --attribute mansource=$(PACKAGE_NAME) \
	    --attribute manversion=$(PACKAGE_VERSION) \
	    --attribute manmanual="Flux Command Reference" \
	    --destination-dir=$(builddir) \
	    --doctype manpage --format manpage $< $(STDERR_DEVNULL)

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc NODESET.adoc

CLEANFILES = $(MAN1_FILES) $(XML_FILES)
