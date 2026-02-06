.NOTPARALLEL:

SUBDIRS = . test

MAN_FILES = $(MAN1_FILES) $(MAN3_FILES) $(MAN5_FILES) $(MAN7_FILES)

#######
# Man 1
#######

MAN1_FILES = $(MAN1_FILES_PRIMARY) $(MAN1_FILES_SECONDARY)

MAN1_FILES_PRIMARY = \
	man1/flux.1 \
	man1/flux-broker.1 \
	man1/flux-kvs.1 \
	man1/flux-keygen.1 \
	man1/flux-logger.1 \
	man1/flux-overlay.1 \
	man1/flux-ping.1 \
	man1/flux-start.1 \
	man1/flux-startlog.1 \
	man1/flux-module.1 \
	man1/flux-exec.1 \
	man1/flux-env.1 \
	man1/flux-getattr.1 \
	man1/flux-dmesg.1 \
	man1/flux-dump.1 \
	man1/flux-fsck.1 \
	man1/flux-archive.1 \
	man1/flux-content.1 \
	man1/flux-config.1 \
	man1/flux-proxy.1 \
	man1/flux-queue.1 \
	man1/flux-cron.1 \
	man1/flux-event.1 \
	man1/flux-submit.1 \
	man1/flux-run.1 \
	man1/flux-bulksubmit.1 \
	man1/flux-alloc.1 \
	man1/flux-batch.1 \
	man1/flux-multi-prog.1 \
	man1/flux-job.1 \
	man1/flux-version.1 \
	man1/flux-jobs.1 \
	man1/flux-pmi.1 \
	man1/flux-pstree.1 \
	man1/flux-restore.1 \
	man1/flux-shell.1 \
	man1/flux-top.1 \
	man1/flux-jobtap.1 \
	man1/flux-shutdown.1 \
	man1/flux-uptime.1 \
	man1/flux-uri.1 \
	man1/flux-resource.1 \
	man1/flux-pgrep.1 \
	man1/flux-cancel.1 \
	man1/flux-watch.1 \
	man1/flux-update.1 \
	man1/flux-hostlist.1 \
	man1/flux-housekeeping.1 \
	man1/flux-modprobe.1 \
	man1/flux-sproc.1

# These files are generated as clones of a primary page.
# Sphinx handles this automatically if declared in the conf.py
MAN1_FILES_SECONDARY = \
	man1/flux-setattr.1 \
	man1/flux-lsattr.1 \
	man1/flux-pkill.1


MAN3_FILES = $(MAN3_FILES_PRIMARY) $(MAN3_FILES_SECONDARY)

MAN3_FILES_PRIMARY = \
	man3/flux_shell_task_channel_subscribe.3 \
	man3/flux_shell_add_completion_ref.3 \
	man3/flux_shell_add_event_context.3 \
	man3/flux_shell_add_event_handler.3 \
	man3/flux_shell_aux_set.3 \
	man3/flux_shell_current_task.3 \
	man3/flux_shell_get_flux.3 \
	man3/flux_shell_get_hwloc_xml.3 \
	man3/flux_shell_get_info.3 \
	man3/flux_shell_get_jobspec_info.3 \
	man3/flux_shell_get_taskmap.3 \
	man3/flux_shell_get_hostlist.3 \
	man3/flux_shell_getenv.3 \
	man3/flux_shell_getopt.3 \
	man3/flux_shell_killall.3 \
	man3/flux_shell_log.3 \
	man3/flux_shell_plugstack_call.3 \
	man3/flux_shell_rpc_pack.3 \
	man3/flux_shell_service_register.3 \
	man3/flux_shell_task_get_info.3 \
	man3/flux_shell_task_subprocess.3 \
	man3/flux_service_register.3 \
	man3/flux_open.3 \
	man3/flux_send.3 \
	man3/flux_recv.3 \
	man3/flux_requeue.3 \
	man3/flux_aux_set.3 \
	man3/flux_flags_set.3 \
	man3/flux_comms_error_set.3 \
	man3/flux_event_subscribe.3 \
	man3/flux_event_publish.3 \
	man3/flux_pollevents.3 \
	man3/flux_msg_encode.3 \
	man3/flux_msg_has_flag.3 \
	man3/flux_rpc.3 \
	man3/flux_get_rank.3 \
	man3/flux_attr_get.3 \
	man3/flux_get_reactor.3 \
	man3/flux_get_conf.3 \
	man3/flux_module_set_running.3 \
	man3/flux_conf_create.3 \
	man3/flux_conf_builtin_get.3 \
	man3/flux_reactor_create.3 \
	man3/flux_fd_watcher_create.3 \
	man3/flux_watcher_start.3 \
	man3/flux_handle_watcher_create.3 \
	man3/flux_timer_watcher_create.3 \
	man3/flux_periodic_watcher_create.3 \
	man3/flux_idle_watcher_create.3 \
	man3/flux_watcher_set_priority.3 \
	man3/flux_msg_handler_create.3 \
	man3/flux_msg_cmp.3 \
	man3/flux_msg_create.3 \
	man3/flux_msg_handler_addvec.3 \
	man3/flux_msg_handler_allow_rolemask.3 \
	man3/flux_signal_watcher_create.3 \
	man3/flux_stat_watcher_create.3 \
	man3/flux_respond.3 \
	man3/flux_reactor_now.3 \
	man3/flux_request_decode.3 \
	man3/flux_event_decode.3 \
	man3/flux_response_decode.3 \
	man3/flux_request_encode.3 \
	man3/flux_log.3 \
	man3/flux_future_get.3 \
	man3/flux_future_create.3 \
	man3/flux_future_wait_all_create.3 \
	man3/flux_future_and_then.3 \
	man3/flux_kvs_lookup.3 \
	man3/flux_kvs_commit.3 \
	man3/flux_kvs_txn_create.3 \
	man3/flux_kvs_namespace_create.3 \
	man3/flux_kvs_getroot.3 \
	man3/flux_kvs_copy.3 \
	man3/flux_core_version.3 \
	man3/hostlist_create.3 \
	man3/idset_create.3 \
	man3/idset_encode.3 \
	man3/idset_decode.3 \
	man3/idset_add.3 \
	man3/idset_alloc.3 \
	man3/flux_jobtap_get_flux.3 \
	man3/flux_sync_create.3 \
	man3/flux_job_timeleft.3


# These files are generated as clones of a primary page.
# Sphinx handles this automatically if declared in the conf.py
MAN3_FILES_SECONDARY = \
	man3/flux_shell_remove_completion_ref.3 \
	man3/flux_shell_aux_get.3 \
	man3/flux_shell_task_first.3 \
	man3/flux_shell_task_next.3 \
	man3/flux_shell_info_unpack.3 \
	man3/flux_shell_jobspec_info_unpack.3 \
	man3/flux_shell_get_rank_info.3 \
	man3/flux_shell_rank_info_unpack.3 \
	man3/flux_shell_get_environ.3 \
	man3/flux_shell_setenvf.3 \
	man3/flux_shell_unsetenv.3 \
	man3/flux_shell_getopt_unpack.3 \
	man3/flux_shell_setopt.3 \
	man3/flux_shell_setopt_pack.3 \
	man3/flux_shell_err.3 \
	man3/flux_shell_fatal.3 \
	man3/flux_shell_raise.3 \
	man3/flux_shell_log_setlevel.3 \
	man3/flux_shell_task_info_unpack.3 \
	man3/flux_shell_task_cmd.3 \
	man3/flux_service_unregister.3 \
	man3/flux_send_new.3 \
	man3/flux_clone.3 \
	man3/flux_close.3 \
	man3/flux_reconnect.3 \
	man3/flux_open_ex.3 \
	man3/flux_aux_get.3 \
	man3/flux_flags_unset.3 \
	man3/flux_flags_get.3 \
	man3/flux_event_unsubscribe.3 \
	man3/flux_event_publish_pack.3 \
	man3/flux_event_publish_raw.3 \
	man3/flux_event_publish_get_seq.3 \
	man3/flux_pollfd.3 \
	man3/flux_msg_decode.3 \
	man3/flux_msg_set_flag.3 \
	man3/flux_msg_clear_flag.3 \
	man3/flux_msg_is_private.3 \
	man3/flux_msg_set_private.3 \
	man3/flux_msg_is_streaming.3 \
	man3/flux_msg_set_streaming.3 \
	man3/flux_msg_is_noresponse.3 \
	man3/flux_msg_set_noresponse.3 \
	man3/flux_get_size.3 \
	man3/flux_attr_set.3 \
	man3/flux_module_debug_test.3 \
	man3/flux_module_config_request_decode.3 \
	man3/flux_set_reactor.3 \
	man3/flux_set_conf_new.3 \
	man3/flux_conf_incref.3 \
	man3/flux_conf_decref.3 \
	man3/flux_conf_copy.3 \
	man3/flux_conf_unpack.3 \
	man3/flux_conf_pack.3 \
	man3/flux_conf_parse.3 \
	man3/flux_reactor_destroy.3 \
	man3/flux_reactor_run.3 \
	man3/flux_reactor_stop.3 \
	man3/flux_reactor_stop_error.3 \
	man3/flux_fd_watcher_get_fd.3 \
	man3/flux_watcher_stop.3 \
	man3/flux_watcher_is_active.3 \
	man3/flux_watcher_destroy.3 \
	man3/flux_watcher_ref.3 \
	man3/flux_watcher_unref.3 \
	man3/flux_watcher_is_referenced.3 \
	man3/flux_watcher_next_wakeup.3 \
	man3/flux_handle_watcher_get_flux.3 \
	man3/flux_timer_watcher_reset.3 \
	man3/flux_periodic_watcher_reset.3 \
	man3/flux_prepare_watcher_create.3 \
	man3/flux_check_watcher_create.3 \
	man3/flux_msg_handler_destroy.3 \
	man3/flux_msg_handler_start.3 \
	man3/flux_msg_handler_stop.3 \
	man3/flux_get_handle_watcher.3 \
	man3/flux_msg_handler_delvec.3 \
	man3/flux_msg_handler_deny_rolemask.3 \
	man3/flux_signal_watcher_get_signum.3 \
	man3/flux_stat_watcher_get_rstat.3 \
	man3/flux_respond_raw.3 \
	man3/flux_respond_pack.3 \
	man3/flux_respond_error.3 \
	man3/flux_reactor_now_update.3 \
	man3/flux_request_unpack.3 \
	man3/flux_request_decode_raw.3 \
	man3/flux_event_unpack.3 \
	man3/flux_event_encode.3 \
	man3/flux_event_pack.3 \
	man3/flux_event_encode_raw.3 \
	man3/flux_event_decode_raw.3 \
	man3/flux_response_decode_raw.3 \
	man3/flux_response_decode_error.3 \
	man3/flux_request_encode_raw.3 \
	man3/flux_vlog.3 \
	man3/flux_log_set_appname.3 \
	man3/flux_log_set_procid.3 \
	man3/flux_future_then.3 \
	man3/flux_future_wait_for.3 \
	man3/flux_future_reset.3 \
	man3/flux_future_destroy.3 \
	man3/flux_future_fulfill.3 \
	man3/flux_future_fulfill_error.3 \
	man3/flux_future_fulfill_with.3 \
	man3/flux_future_aux_set.3 \
	man3/flux_future_aux_get.3 \
	man3/flux_future_set_flux.3 \
	man3/flux_future_get_flux.3 \
	man3/flux_future_wait_any_create.3 \
	man3/flux_future_push.3 \
	man3/flux_future_first_child.3 \
	man3/flux_future_next_child.3 \
	man3/flux_future_get_child.3 \
	man3/flux_future_or_then.3 \
	man3/flux_future_continue.3 \
	man3/flux_future_continue_error.3 \
	man3/flux_msg_copy.3 \
	man3/flux_msg_incref.3 \
	man3/flux_msg_decref.3 \
	man3/flux_msg_destroy.3 \
	man3/flux_rpc_pack.3 \
	man3/flux_rpc_raw.3 \
	man3/flux_rpc_message.3 \
	man3/flux_rpc_get.3 \
	man3/flux_rpc_get_unpack.3 \
	man3/flux_rpc_get_raw.3 \
	man3/flux_rpc_get_matchtag.3 \
	man3/flux_rpc_get_nodeid.3 \
	man3/flux_kvs_lookupat.3 \
	man3/flux_kvs_lookup_get.3 \
	man3/flux_kvs_lookup_get_unpack.3 \
	man3/flux_kvs_lookup_get_raw.3 \
	man3/flux_kvs_lookup_get_dir.3 \
	man3/flux_kvs_lookup_get_treeobj.3 \
	man3/flux_kvs_lookup_get_symlink.3 \
	man3/flux_kvs_getroot_get_treeobj.3 \
	man3/flux_kvs_getroot_get_blobref.3 \
	man3/flux_kvs_getroot_get_sequence.3 \
	man3/flux_kvs_getroot_get_owner.3 \
	man3/flux_kvs_getroot_cancel.3 \
	man3/flux_kvs_commit_get_treeobj.3 \
	man3/flux_kvs_commit_get_rootref.3 \
	man3/flux_kvs_commit_get_sequence.3 \
	man3/flux_kvs_txn_destroy.3 \
	man3/flux_kvs_txn_put.3 \
	man3/flux_kvs_txn_pack.3 \
	man3/flux_kvs_txn_vpack.3 \
	man3/flux_kvs_txn_mkdir.3 \
	man3/flux_kvs_txn_unlink.3 \
	man3/flux_kvs_txn_symlink.3 \
	man3/flux_kvs_txn_put_raw.3 \
	man3/flux_kvs_txn_put_treeobj.3 \
	man3/flux_kvs_namespace_remove.3 \
	man3/flux_kvs_move.3 \
	man3/flux_core_version_string.3 \
	man3/hostlist_destroy.3 \
	man3/hostlist_decode.3 \
	man3/hostlist_encode.3 \
	man3/hostlist_copy.3 \
	man3/hostlist_append.3 \
	man3/hostlist_append_list.3 \
	man3/hostlist_nth.3 \
	man3/hostlist_find.3 \
	man3/hostlist_delete.3 \
	man3/hostlist_count.3 \
	man3/hostlist_sort.3 \
	man3/hostlist_uniq.3 \
	man3/hostlist_first.3 \
	man3/hostlist_last.3 \
	man3/hostlist_next.3 \
	man3/hostlist_current.3 \
	man3/hostlist_remove_current.3 \
	man3/idset_destroy.3 \
	man3/idset_decode_ex.3 \
	man3/idset_decode_empty.3 \
	man3/idset_decode_info.3 \
	man3/idset_decode_add.3 \
	man3/idset_decode_subtract.3 \
	man3/idset_copy.3 \
	man3/idset_test.3 \
	man3/idset_set.3 \
	man3/idset_range_set.3 \
	man3/idset_clear.3 \
	man3/idset_range_clear.3 \
	man3/idset_first.3 \
	man3/idset_last.3 \
	man3/idset_next.3 \
	man3/idset_prev.3 \
	man3/idset_empty.3 \
	man3/idset_universe_size.3 \
	man3/idset_count.3 \
	man3/idset_equal.3 \
	man3/idset_subtract.3 \
	man3/idset_union.3 \
	man3/idset_difference.3 \
	man3/idset_intersect.3 \
	man3/idset_has_intersection.3 \
	man3/idset_clear_all.3 \
	man3/idset_free.3 \
	man3/idset_free_check.3 \
	man3/flux_jobtap_service_register.3 \
	man3/flux_jobtap_reprioritize_all.3 \
	man3/flux_jobtap_reprioritize_job.3 \
	man3/flux_jobtap_priority_unavail.3 \
	man3/flux_jobtap_reject_job.3

MAN5_FILES = $(MAN5_FILES_PRIMARY)

MAN5_FILES_PRIMARY = \
	man5/flux-shell-initrc.5 \
	man5/flux-config.5 \
	man5/flux-config-access.5 \
	man5/flux-config-bootstrap.5 \
	man5/flux-config-tbon.5 \
	man5/flux-config-exec.5 \
	man5/flux-config-systemd.5 \
	man5/flux-config-resource.5 \
	man5/flux-config-job-manager.5 \
	man5/flux-config-ingest.5 \
	man5/flux-config-kvs.5 \
	man5/flux-config-policy.5 \
	man5/flux-config-queues.5 \
	man5/flux-config-heartbeat.5


MAN7_FILES = $(MAN7_FILES_PRIMARY)

MAN7_FILES_PRIMARY = \
	man7/flux-broker-attributes.7 \
	man7/flux-jobtap-plugins.7 \
	man7/flux-shell-plugins.7 \
	man7/flux-shell-options.7 \
	man7/flux-environment.7


RST_FILES  = \
	$(MAN1_FILES_PRIMARY:.1=.rst) \
	$(MAN3_FILES_PRIMARY:.3=.rst) \
	$(MAN5_FILES_PRIMARY:.5=.rst) \
	$(MAN7_FILES_PRIMARY:.7=.rst)

if ENABLE_DOCS
man_MANS = $(MAN1_FILES) $(MAN3_FILES) $(MAN5_FILES) $(MAN7_FILES)
$(RST_FILES): \
	man1/common/resources.rst \
	man1/common/experimental.rst \
	man1/common/job-param-additional.rst \
	man1/common/job-param-batch.rst \
	man1/common/job-param-common.rst \
	man1/common/job-param-pertask.rst \
	man1/common/job-param-perres.rst \
	man1/common/job-standard-io.rst \
	man1/common/job-constraints.rst \
	man1/common/job-dependencies.rst \
	man1/common/job-environment.rst \
	man1/common/job-env-rules.rst \
	man1/common/job-environment-variables.rst \
	man1/common/job-process-resource-limits.rst \
	man1/common/job-other-options.rst \
	man1/common/job-other-batch.rst \
	man1/common/job-other-run.rst \
	man1/common/job-shell-options.rst \
	man1/common/job-mustache-templates.rst \
	man3/common/resources.rst \
	man3/common/experimental.rst \
	man3/common/json_pack.rst \
	man3/common/json_unpack.rst \
	man5/common/resources.rst \
	man5/common/experimental.rst \
	man7/common/resources.rst \
	man7/common/experimental.rst
endif

SUFFIXES = .rst .1 .3 .5 .7

sphinx_man = $(sphinx_man_$(V))
sphinx_man_ = $(sphinx_man_$(AM_DEFAULT_VERBOSITY))
sphinx_man_0 = @echo "  BUILD     manpages";

sphinx_html = $(sphinx_html_$(V))
sphinx_html_ = $(sphinx_html_$(AM_DEFAULT_VERBOSITY))
sphinx_html_0 = @echo "  BUILD     html";

sphinx_verbose_flags = $(sphinx_verbose_flags_$(V))
sphinx_verbose_flags_ = $(sphinx_verbose_flags_$(AM_DEFAULT_VERBOSITY))
sphinx_verbose_flags_0 =
sphinx_verbose_flags_1 = -v
sphinx_verbose_flags_2 = -vv

STDERR_DEVNULL = $(stderr_devnull_$(V))
stderr_devnull_ =  $(stderr_devnull_$(AM_DEFAULT_VERBOSITY))
stderr_devnull_0 = >/dev/null 2>&1

$(MAN_FILES): manpages.py conf.py $(RST_FILES)
	$(sphinx_man) \
	SPHINX_BUILDDIR=$(abs_builddir) $(PYTHON) \
		-m sphinx $(sphinx_verbose_flags) -b man $(srcdir) ./man \
		$(STDERR_DEVNULL)
	@echo "  MV        manpages"; \
	for sec in 1 3 5 7; do \
	  $(MKDIR_P) man$$sec && \
	  mv -f $(abs_builddir)/man/*.$$sec man$$sec/; \
	done

.PHONY: html
html: conf.py $(RST_FILES)
	$(sphinx_html) \
	SPHINX_BUILDDIR=$(abs_builddir) $(PYTHON) \
		-m sphinx $(sphinx_verbose_flags) -b html $(srcdir) ./html \
		$(STDERR_DEVNULL)

EXTRA_DIST = \
	conf.py \
	manpages.py \
	domainrefs.py \
	index.rst \
	index_man.rst \
	requirements.txt \
	guide/build.rst \
	guide/support.rst \
	guide/images/lgplv3-147x51.png \
	guide/images/adminarch.dia \
	guide/images/adminarch.png \
	guide/images/fox-standing.svg \
	guide/images/Frog-1.svg \
	guide/images/states.dot \
	guide/images/states.png \
	guide/images/states_norm.dot \
	guide/images/states_norm.png \
	guide/start.rst \
	guide/interact.rst \
	guide/admin.rst \
	guide/admin_slurm.rst \
	guide/admin_background.rst \
	guide/admin_config.rst \
	guide/admin_daily.rst \
	guide/admin_install.rst \
	guide/admin_preflight.rst \
	guide/admin_trouble.rst \
	guide/glossary.rst \
	guide/internals.rst \
	guide/debug.rst \
	guide/kvs.rst \
	guide/broker.rst \
	guide/troubleshooting.rst \
	guide/workflows.rst \
	$(RST_FILES) \
	man1/index.rst \
	man1/common/resources.rst \
	man1/common/experimental.rst \
	man1/common/job-param-additional.rst \
	man1/common/job-param-batch.rst \
	man1/common/job-param-common.rst \
	man1/common/job-param-pertask.rst \
	man1/common/job-param-perres.rst \
	man1/common/job-standard-io.rst \
	man1/common/job-constraints.rst \
	man1/common/job-dependencies.rst \
	man1/common/job-environment.rst \
	man1/common/job-env-rules.rst \
	man1/common/job-environment-variables.rst \
	man1/common/job-process-resource-limits.rst \
	man1/common/job-other-options.rst \
	man1/common/job-other-batch.rst \
	man1/common/job-other-run.rst \
	man1/common/job-shell-options.rst \
	man1/common/job-mustache-templates.rst \
	man3/common/resources.rst \
	man3/common/experimental.rst \
	man3/index.rst \
	man3/common/json_pack.rst \
	man3/common/json_unpack.rst \
	man5/common/resources.rst \
	man5/common/experimental.rst \
	man5/index.rst \
	man7/index.rst \
	man7/common/resources.rst \
	man7/common/experimental.rst \
	man7/flux-undocumented.rst

CLEANFILES = \
	$(MAN_FILES)

clean-local:
	-rm -rf man python/autogenerated

distclean-local:
	-rm -rf doctrees

# test programs from man3
AM_CFLAGS = \
	$(WARNING_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

AM_LDFLAGS = \
	$(CODE_COVERAGE_LIBS) \
	-no-install

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/include \
	-I$(top_builddir)/src/common/libflux

LDADD = \
	$(top_builddir)/src/common/libflux-core.la \
	$(top_builddir)/src/common/libflux-internal.la \
	$(LIBPTHREAD)

check_PROGRAMS = \
	man3/example/open \
	man3/example/send \
	man3/example/recv \
	man3/example/event \
	man3/example/sync \
	man3/example/rpc \
	man3/example/rpc_then \
	man3/example/info

check_HEADERS = man3/example/die.h
