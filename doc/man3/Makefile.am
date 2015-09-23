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
	flux_pollevents.3 \
	flux_msg_encode.3 \
	flux_msg_sendfd.3 \
	flux_rpc.3 \
	flux_rpc_then.3 \
	flux_rpc_multi.3 \
	flux_reduce_create.3 \
	flux_get_rank.3 \
	flux_attr_get.3 \
	flux_get_reactor.3 \
	flux_reactor_create.3

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
	flux_pollfd.3 \
	flux_msg_decode.3 \
	flux_msg_recvfd.3 \
	flux_rpc_destroy.3 \
	flux_rpc_check.3 \
	flux_rpc_get.3 \
	flux_rpc_completed.3 \
	flux_reduce_destroy.3 \
	flux_reduce_append.3 \
	flux_reduce_pop.3 \
	flux_reduce_push.3 \
	flux_reduce_opt_set.3 \
	flux_reduce_opt_get.3 \
	flux_get_size.3 \
	flux_get_arity.3 \
	flux_attr_set.3 \
	flux_attr_first.3 \
	flux_attr_next.3 \
	flux_set_reactor.3 \
	flux_reactor_destroy.3 \
	flux_reactor_run.3 \
	flux_reactor_stop.3 \
	flux_reactor_stop_error.3

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
flux_msg_decode.3: flux_msg_encode.3
flux_msg_recvfd.3: flux_msg_sendfd.3
flux_rpc_destroy.3: flux_rpc.3
flux_rpc_get.3: flux_rpc.3
flux_rpc_check.3: flux_rpc_then.3
flux_rpc_completed.3: flux_rpc_multi.3
flux_reduce_destroy.3: flux_reduce_create.3
flux_reduce_append.3: flux_reduce_create.3
flux_reduce_pop.3: flux_reduce_create.3
flux_reduce_push.3: flux_reduce_create.3
flux_reduce_opt_set.3: flux_reduce_create.3
flux_reduce_opt_get.3: flux_reduce_create.3
flux_get_size.3: flux_get_rank.3
flux_get_arity.3: flux_get_rank.3
flux_attr_set.3: flux_attr_get.3
flux_attr_first.3: flux_attr_get.3
flux_attr_next.3: flux_attr_get.3
flux_set_reactor.3: flux_get_reactor.3
flux_reactor_destroy.3: flux_reactor_create.3
flux_reactor_run.3: flux_reactor_create.3
flux_reactor_stop.3: flux_reactor_create.3
flux_reactor_error.3: flux_reactor_create.3

flux_open.3: topen.c
flux_send.3: tsend.c
flux_recv.3: trecv.c
flux_event_subscribe.3: tevent.c
flux_rpc.3: trpc.c
flux_rpc_then.3: trpc_then.c
flux_rpc_multi.3: trpc_then_multi.c
flux_reduce_create.3: treduce.c
flux_get_rank.3: tinfo.c

EXTRA_DIST = $(ADOC_FILES) COPYRIGHT.adoc

CLEANFILES = $(MAN3_FILES) $(XML_FILES)

AM_CFLAGS = \
	@GCCWARN@ \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LDFLAGS)

AM_CPPFLAGS = \
        $(JSON_CFLAGS) $(ZMQ_CFLAGS) \
        -I$(top_srcdir) -I$(top_srcdir)/src/include

LDADD = \
        $(top_builddir)/src/common/libflux-internal.la \
        $(top_builddir)/src/common/libflux-core.la \
        $(JSON_LIBS) $(ZMQ_LIBS) $(LIBPTHREAD)

check_PROGRAMS = \
	topen \
	tsend \
	trecv \
	tevent \
	trpc \
	trpc_then \
	trpc_then_multi \
	tinfo

check_LTLIBRARIES = \
	treduce.la

treduce_la_SOURCES = treduce.c
treduce_la_LDFLAGS = -module -rpath /nowhere \
        -export-symbols-regex '^mod_(main|name)$$' \
	-export-sym--disable-static -avoid-version -shared -export-dynamic
