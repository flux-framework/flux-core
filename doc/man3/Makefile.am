MAN3_FILES = $(MAN3_FILES_PRIMARY) $(MAN3_FILES_SECONDARY)

MAN3_FILES_PRIMARY = \
	flux_open.3 \
	flux_send.3 \
	flux_recv.3 \
	flux_requeue.3 \
	flux_aux_set.3 \
	flux_flags_set.3 \
	flux_fatal_set.3 \
	flux_event_subscribe.3 \
	flux_pollevents.3

# These files are generated as roff .so includes of a primary page.
# A2X handles this automatically if mentioned in NAME section
MAN3_FILES_SECONDARY = \
	flux_close.3 \
	flux_aux_get.3 \
	flux_flags_unset.3 \
	flux_flags_get.3 \
	flux_fatal_error.3 \
	FLUX_FATAL.3 \
	flux_event_unsubscribe.3 \
	flux_pollfd.3

ADOC_FILES  = $(MAN3_FILES_PRIMARY:%.3=%.adoc)
XML_FILES   = $(MAN3_FILES_PRIMARY:%.3=%.xml)

if HAVE_A2X
dist_man_MANS = $(MAN3_FILES)
$(MAN3_FILES): COPYRIGHT.adoc
endif

SUFFIXES = .adoc .3

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = 2>/dev/null

.adoc.3:
	$(AM_V_GEN)$(A2X) --attribute mansource=$(META_NAME) \
	    --attribute manversion=$(META_VERSION) \
	    --attribute manmanual="Flux Programming Reference" \
	    --destination-dir=$(builddir) \
	    --doctype manpage --format manpage $< $(STDERR_DEVNULL)

flux_close.3: flux_open.3
flux_aux_get.3: flux_aux_set.3
flux_flags_unset.3: flux_flags_set.3
flux_flags_get.3: flux_flags_set.3
flux_fatal_error.3: flux_fatal_set.3
FLUX_FATAL.3: flux_fatal_set.3
flux_event_unsubscribe.3: flux_event_subscribe.3
flux_pollfd.3: flux_pollevents.3

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc

CLEANFILES = $(MAN3_FILES) $(XML_FILES)

TESTS_ENVIRONMENT = \
	ASPELL=$(ASPELL) \
	pws_dict=$(abs_top_srcdir)/doc/test/spell.en.pws \
	man_dir=$(abs_srcdir)

TESTS = $(abs_top_srcdir)/doc/test/spellcheck
