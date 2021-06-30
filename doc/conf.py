###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys

# -- Project information -----------------------------------------------------

project = 'flux-core'
copyright = '''Copyright 2014 Lawrence Livermore National Security, LLC and Flux developers.

SPDX-License-Identifier: LGPL-3.0'''
author = 'This page is maintained by the Flux community.'

# -- General configuration ---------------------------------------------------

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

master_doc = 'index'
source_suffix = '.rst'

extensions = [
    'sphinx.ext.intersphinx',
    'sphinx.ext.napoleon'
]

# Disable "smartquotes" to avoid things such as turning long-options
#  "--" into en-dash in html output, which won't make much sense for
#  manpages.
smartquotes = False

# -- Setup for Sphinx API Docs -----------------------------------------------

# Workaround since sphinx does not automatically run apidoc before a build
# Copied from https://github.com/readthedocs/readthedocs.org/issues/1139

script_dir = os.path.normpath(os.path.dirname(__file__))
py_bindings_dir = os.path.normpath(os.path.join(script_dir, "../src/bindings/python/"))

# Make sure that the python bindings are in PYTHONPATH for autodoc
sys.path.insert(0, py_bindings_dir)

# run api doc
def run_apidoc(_):
    # Move import inside so that `gen-cmdhelp.py` can exec this file in LGTM.com
    # without sphinx installed
    # pylint: disable=import-outside-toplevel
    from sphinx.ext.apidoc import main

    try:
        # Check if running under `make`
        build_dir = os.path.normpath(os.environ.get('SPHINX_BUILDDIR'))
    except:
        build_dir = script_dir
    output_path = os.path.join(build_dir, 'python')
    exclusions = [os.path.join(py_bindings_dir, 'setup.py'),]
    main(['-e', '-f', '-M', '-o', output_path, py_bindings_dir] + exclusions)

# launch setup
def setup(app):
    app.connect('builder-inited', run_apidoc)

# ReadTheDocs runs sphinx without first building Flux, so the cffi modules in
# `_flux` will not exist, causing import errors.  Mock the imports to prevent
# these errors.

autodoc_mock_imports = ["_flux", "flux.constants", "yaml"]

napoleon_google_docstring = True

# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = 'sphinx_rtd_theme'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
# html_static_path = ['_static']

# -- Options for man output -------------------------------------------------

# Add man page entries with the following information:
# - Relative file path (without .rst extension)
# - Man page name
# - Man page description
# - Author (use [author])
# - Manual section
man_pages = [
    ('man1/flux-broker', 'flux-broker', 'Flux message broker daemon', [author], 1),
    ('man1/flux-content', 'flux-content', 'access content service', [author], 1),
    ('man1/flux-cron', 'flux-cron', 'Cron-like utility for Flux', [author], 1),
    ('man1/flux-dmesg', 'flux-dmesg', 'access broker ring buffer', [author], 1),
    ('man1/flux-env', 'flux-env', 'Print the flux environment or execute a command inside it', [author], 1),
    ('man1/flux-event', 'flux-event', 'Send and receive Flux events', [author], 1),
    ('man1/flux-exec', 'flux-exec', 'Execute processes across flux ranks', [author], 1),
    ('man1/flux-getattr', 'flux-setattr', 'access broker attributes', [author], 1),
    ('man1/flux-getattr', 'flux-lsattr', 'access broker attributes', [author], 1),
    ('man1/flux-getattr', 'flux-getattr', 'access broker attributes', [author], 1),
    ('man1/flux-hwloc', 'flux-hwloc', 'Control/query resource-hwloc service', [author], 1),
    ('man1/flux-jobs', 'flux-jobs', 'list jobs submitted to Flux', [author], 1),
    ('man1/flux-jobtap', 'flux-jobtap', 'List, remove, and load job-manager plugins', [author], 1),
    ('man1/flux-keygen', 'flux-keygen', 'generate keys for Flux security', [author], 1),
    ('man1/flux-kvs', 'flux-kvs', 'Flux key-value store utility', [author], 1),
    ('man1/flux-logger', 'flux-logger', 'create a Flux log entry', [author], 1),
    ('man1/flux-mini', 'flux-mini', 'Minimal Job Submission Tool', [author], 1),
    ('man1/flux-job', 'flux-job', 'Job Housekeeping Tool', [author], 1),
    ('man1/flux-module', 'flux-module', 'manage Flux extension modules', [author], 1),
    ('man1/flux-ping', 'flux-ping', 'measure round-trip latency to Flux services', [author], 1),
    ('man1/flux-proxy', 'flux-proxy', 'create proxy environment for Flux instance', [author], 1),
    ('man1/flux-start', 'flux-start', 'bootstrap a local Flux instance', [author], 1),
    ('man1/flux-version', 'flux-version', 'Display flux version information', [author], 1),
    ('man1/flux', 'flux', 'the Flux resource management framework', [author], 1),
    ('man1/flux-shell', 'flux-shell', 'the Flux job shell', [author], 1),
    ('man3/flux_attr_get', 'flux_attr_set', 'get/set Flux broker attributes', [author], 3),
    ('man3/flux_attr_get', 'flux_attr_get', 'get/set Flux broker attributes', [author], 3),
    ('man3/flux_aux_set', 'flux_aux_get', 'get/set auxiliary handle data', [author], 3),
    ('man3/flux_aux_set', 'flux_aux_set', 'get/set auxiliary handle data', [author], 3),
    ('man3/flux_child_watcher_create', 'flux_child_watcher_get_rpid', 'create child watcher', [author], 3),
    ('man3/flux_child_watcher_create', 'flux_child_watcher_get_rstatus', 'create child watcher', [author], 3),
    ('man3/flux_child_watcher_create', 'flux_child_watcher_create', 'create child watcher', [author], 3),
    ('man3/flux_content_load', 'flux_content_load_get', 'load/store content', [author], 3),
    ('man3/flux_content_load', 'flux_content_store', 'load/store content', [author], 3),
    ('man3/flux_content_load', 'flux_content_store_get', 'load/store content', [author], 3),
    ('man3/flux_content_load', 'flux_content_load', 'load/store content', [author], 3),
    ('man3/flux_core_version', 'flux_core_version_string', 'get flux-core version', [author], 3),
    ('man3/flux_core_version', 'flux_core_version', 'get flux-core version', [author], 3),
    ('man3/flux_event_decode', 'flux_event_decode_raw', 'encode/decode a Flux event message', [author], 3),
    ('man3/flux_event_decode', 'flux_event_unpack', 'encode/decode a Flux event message', [author], 3),
    ('man3/flux_event_decode', 'flux_event_encode', 'encode/decode a Flux event message', [author], 3),
    ('man3/flux_event_decode', 'flux_event_encode_raw', 'encode/decode a Flux event message', [author], 3),
    ('man3/flux_event_decode', 'flux_event_pack', 'encode/decode a Flux event message', [author], 3),
    ('man3/flux_event_decode', 'flux_event_decode', 'encode/decode a Flux event message', [author], 3),
    ('man3/flux_event_publish', 'flux_event_publish_pack', 'publish events', [author], 3),
    ('man3/flux_event_publish', 'flux_event_publish_raw', 'publish events', [author], 3),
    ('man3/flux_event_publish', 'flux_event_publish_get_seq', 'publish events', [author], 3),
    ('man3/flux_event_publish', 'flux_event_publish', 'publish events', [author], 3),
    ('man3/flux_event_subscribe', 'flux_event_unsubscribe', 'manage subscriptions', [author], 3),
    ('man3/flux_event_subscribe', 'flux_event_subscribe', 'manage subscriptions', [author], 3),
    ('man3/flux_fatal_set', 'flux_fatal_error', 'register/call fatal error function', [author], 3),
    ('man3/flux_fatal_set', 'FLUX_FATAL', 'register/call fatal error function', [author], 3),
    ('man3/flux_fatal_set', 'flux_fatal_set', 'register/call fatal error function', [author], 3),
    ('man3/flux_fd_watcher_create', 'flux_fd_watcher_get_fd', 'create file descriptor watcher', [author], 3),
    ('man3/flux_fd_watcher_create', 'flux_fd_watcher_create', 'create file descriptor watcher', [author], 3),
    ('man3/flux_flags_set', 'flux_flags_unset', 'manipulate Flux handle flags', [author], 3),
    ('man3/flux_flags_set', 'flux_flags_get', 'manipulate Flux handle flags', [author], 3),
    ('man3/flux_flags_set', 'flux_flags_set', 'manipulate Flux handle flags', [author], 3),
    ('man3/flux_future_and_then', 'flux_future_or_then', 'functions for sequential composition of futures', [author], 3),
    ('man3/flux_future_and_then', 'flux_future_continue', 'functions for sequential composition of futures', [author], 3),
    ('man3/flux_future_and_then', 'flux_future_continue_error', 'functions for sequential composition of futures', [author], 3),
    ('man3/flux_future_and_then', 'flux_future_and_then', 'functions for sequential composition of futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_fulfill', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_fulfill_error', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_fulfill_with', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_aux_get', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_aux_set', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_set_flux', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_get_flux', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_create', 'flux_future_create', 'support methods for classes that return futures', [author], 3),
    ('man3/flux_future_get', 'flux_future_then', 'synchronize an activity', [author], 3),
    ('man3/flux_future_get', 'flux_future_wait_for', 'synchronize an activity', [author], 3),
    ('man3/flux_future_get', 'flux_future_reset', 'synchronize an activity', [author], 3),
    ('man3/flux_future_get', 'flux_future_destroy', 'synchronize an activity', [author], 3),
    ('man3/flux_future_get', 'flux_future_get', 'synchronize an activity', [author], 3),
    ('man3/flux_future_wait_all_create', 'flux_future_wait_any_create', 'functions for future composition', [author], 3),
    ('man3/flux_future_wait_all_create', 'flux_future_push', 'functions for future composition', [author], 3),
    ('man3/flux_future_wait_all_create', 'flux_future_first_child', 'functions for future composition', [author], 3),
    ('man3/flux_future_wait_all_create', 'flux_future_next_child', 'functions for future composition', [author], 3),
    ('man3/flux_future_wait_all_create', 'flux_future_get_child', 'functions for future composition', [author], 3),
    ('man3/flux_future_wait_all_create', 'flux_future_wait_all_create', 'functions for future composition', [author], 3),
    ('man3/flux_get_rank', 'flux_get_size', 'query Flux broker info', [author], 3),
    ('man3/flux_get_rank', 'flux_get_rank', 'query Flux broker info', [author], 3),
    ('man3/flux_get_reactor', 'flux_set_reactor', 'get/set reactor associated with broker handle', [author], 3),
    ('man3/flux_get_reactor', 'flux_get_reactor', 'get/set reactor associated with broker handle', [author], 3),
    ('man3/flux_handle_watcher_create', 'flux_handle_watcher_get_flux', 'create broker handle watcher', [author], 3),
    ('man3/flux_handle_watcher_create', 'flux_handle_watcher_create', 'create broker handle watcher', [author], 3),
    ('man3/flux_idle_watcher_create', 'flux_prepare_watcher_create', 'create prepare/check/idle watchers', [author], 3),
    ('man3/flux_idle_watcher_create', 'flux_check_watcher_create', 'create prepare/check/idle watchers', [author], 3),
    ('man3/flux_idle_watcher_create', 'flux_idle_watcher_create', 'create prepare/check/idle watchers', [author], 3),
    ('man3/flux_kvs_commit', 'flux_kvs_fence', 'commit a KVS transaction', [author], 3),
    ('man3/flux_kvs_commit', 'flux_kvs_commit_get_treeobj', 'commit a KVS transaction', [author], 3),
    ('man3/flux_kvs_commit', 'flux_kvs_commit_get_sequence', 'commit a KVS transaction', [author], 3),
    ('man3/flux_kvs_commit', 'flux_kvs_commit', 'commit a KVS transaction', [author], 3),
    ('man3/flux_kvs_copy', 'flux_kvs_move', 'copy/move a KVS key', [author], 3),
    ('man3/flux_kvs_copy', 'flux_kvs_copy', 'copy/move a KVS key', [author], 3),
    ('man3/flux_kvs_getroot', 'flux_kvs_getroot_get_treeobj', 'look up KVS root hash', [author], 3),
    ('man3/flux_kvs_getroot', 'flux_kvs_getroot_get_blobref', 'look up KVS root hash', [author], 3),
    ('man3/flux_kvs_getroot', 'flux_kvs_getroot_get_sequence', 'look up KVS root hash', [author], 3),
    ('man3/flux_kvs_getroot', 'flux_kvs_getroot_get_owner', 'look up KVS root hash', [author], 3),
    ('man3/flux_kvs_getroot', 'flux_kvs_getroot_cancel', 'look up KVS root hash', [author], 3),
    ('man3/flux_kvs_getroot', 'flux_kvs_getroot', 'look up KVS root hash', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookupat', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookup_get', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookup_get_unpack', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookup_get_raw', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookup_get_dir', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookup_get_treeobj', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookup_get_symlink', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_lookup', 'flux_kvs_lookup', 'look up KVS key', [author], 3),
    ('man3/flux_kvs_namespace_create', 'flux_kvs_namespace_create', 'create/remove a KVS namespace', [author], 3),
    ('man3/flux_kvs_namespace_create', 'flux_kvs_namespace_remove', 'create/remove a KVS namespace', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_destroy', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_put', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_pack', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_vpack', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_mkdir', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_unlink', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_symlink', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_put_raw', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_put_treeobj', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_kvs_txn_create', 'flux_kvs_txn_create', 'operate on a KVS transaction object', [author], 3),
    ('man3/flux_log', 'flux_vlog', 'Log messages to the Flux Message Broker', [author], 3),
    ('man3/flux_log', 'flux_log_set_appname', 'Log messages to the Flux Message Broker', [author], 3),
    ('man3/flux_log', 'flux_log_set_procid', 'Log messages to the Flux Message Broker', [author], 3),
    ('man3/flux_log', 'flux_log', 'Log messages to the Flux Message Broker', [author], 3),
    ('man3/flux_msg_cmp', 'flux_msg_cmp', 'match a message', [author], 3),
    ('man3/flux_msg_encode', 'flux_msg_decode', 'convert a Flux message to buffer and back again', [author], 3),
    ('man3/flux_msg_encode', 'flux_msg_encode', 'convert a Flux message to buffer and back again', [author], 3),
    ('man3/flux_msg_handler_addvec', 'flux_msg_handler_delvec', 'bulk add/remove message handlers', [author], 3),
    ('man3/flux_msg_handler_addvec', 'flux_msg_handler_addvec', 'bulk add/remove message handlers', [author], 3),
    ('man3/flux_msg_handler_create', 'flux_msg_handler_destroy', 'manage message handlers', [author], 3),
    ('man3/flux_msg_handler_create', 'flux_msg_handler_start', 'manage message handlers', [author], 3),
    ('man3/flux_msg_handler_create', 'flux_msg_handler_stop', 'manage message handlers', [author], 3),
    ('man3/flux_msg_handler_create', 'flux_msg_handler_create', 'manage message handlers', [author], 3),
    ('man3/flux_open', 'flux_clone', 'open/close connection to Flux Message Broker', [author], 3),
    ('man3/flux_open', 'flux_close', 'open/close connection to Flux Message Broker', [author], 3),
    ('man3/flux_open', 'flux_open', 'open/close connection to Flux Message Broker', [author], 3),
    ('man3/flux_periodic_watcher_create', 'flux_periodic_watcher_reset', 'set/reset a timer', [author], 3),
    ('man3/flux_periodic_watcher_create', 'flux_periodic_watcher_create', 'set/reset a timer', [author], 3),
    ('man3/flux_pollevents', 'flux_pollfd', 'poll Flux broker handle', [author], 3),
    ('man3/flux_pollevents', 'flux_pollevents', 'poll Flux broker handle', [author], 3),
    ('man3/flux_reactor_create', 'flux_reactor_destroy', 'create/destroy/control event reactor object', [author], 3),
    ('man3/flux_reactor_create', 'flux_reactor_run', 'create/destroy/control event reactor object', [author], 3),
    ('man3/flux_reactor_create', 'flux_reactor_stop', 'create/destroy/control event reactor object', [author], 3),
    ('man3/flux_reactor_create', 'flux_reactor_stop_error', 'create/destroy/control event reactor object', [author], 3),
    ('man3/flux_reactor_create', 'flux_reactor_active_incref', 'create/destroy/control event reactor object', [author], 3),
    ('man3/flux_reactor_create', 'flux_reactor_active_decref', 'create/destroy/control event reactor object', [author], 3),
    ('man3/flux_reactor_create', 'flux_reactor_create', 'create/destroy/control event reactor object', [author], 3),
    ('man3/flux_reactor_now', 'flux_reactor_now_update', 'get/update reactor time', [author], 3),
    ('man3/flux_reactor_now', 'flux_reactor_now', 'get/update reactor time', [author], 3),
    ('man3/flux_recv', 'flux_recv', 'receive message using Flux Message Broker', [author], 3),
    ('man3/flux_request_decode', 'flux_request_unpack', 'decode a Flux request message', [author], 3),
    ('man3/flux_request_decode', 'flux_request_decode_raw', 'decode a Flux request message', [author], 3),
    ('man3/flux_request_decode', 'flux_request_decode', 'decode a Flux request message', [author], 3),
    ('man3/flux_request_encode', 'flux_request_encode_raw', 'encode a Flux request message', [author], 3),
    ('man3/flux_request_encode', 'flux_request_encode', 'encode a Flux request message', [author], 3),
    ('man3/flux_requeue', 'flux_requeue', 'requeue a message', [author], 3),
    ('man3/flux_respond', 'flux_respond_pack', 'respond to a request', [author], 3),
    ('man3/flux_respond', 'flux_respond_raw', 'respond to a request', [author], 3),
    ('man3/flux_respond', 'flux_respond_error', 'respond to a request', [author], 3),
    ('man3/flux_respond', 'flux_respond', 'respond to a request', [author], 3),
    ('man3/flux_response_decode', 'flux_response_decode_raw', 'decode a Flux response message', [author], 3),
    ('man3/flux_response_decode', 'flux_response_decode_error', 'decode a Flux response message', [author], 3),
    ('man3/flux_response_decode', 'flux_response_decode', 'decode a Flux response message', [author], 3),
    ('man3/flux_rpc', 'flux_rpc_pack', 'perform a remote procedure call to a Flux service', [author], 3),
    ('man3/flux_rpc', 'flux_rpc_raw', 'perform a remote procedure call to a Flux service', [author], 3),
    ('man3/flux_rpc', 'flux_rpc_message', 'perform a remote procedure call to a Flux service', [author], 3),
    ('man3/flux_rpc', 'flux_rpc_get', 'perform a remote procedure call to a Flux service', [author], 3),
    ('man3/flux_rpc', 'flux_rpc_get_unpack', 'perform a remote procedure call to a Flux service', [author], 3),
    ('man3/flux_rpc', 'flux_rpc_get_raw', 'perform a remote procedure call to a Flux service', [author], 3),
    ('man3/flux_rpc', 'flux_rpc', 'perform a remote procedure call to a Flux service', [author], 3),
    ('man3/flux_send', 'flux_send', 'send message using Flux Message Broker', [author], 3),
    ('man3/flux_shell_add_completion_ref', 'flux_shell_remove_completion_ref', 'Manipulate conditions for job completion.', [author], 3),
    ('man3/flux_shell_add_completion_ref', 'flux_shell_add_completion_ref', 'Manipulate conditions for job completion.', [author], 3),
    ('man3/flux_shell_add_event_context', 'flux_shell_add_event_context', 'Add context information for standard shell events', [author], 3),
    ('man3/flux_shell_add_event_handler', 'flux_shell_add_event_handler', 'Add an event handler for a shell event', [author], 3),
    ('man3/flux_shell_aux_set', 'flux_shell_aux_get', 'get/set auxiliary handle data', [author], 3),
    ('man3/flux_shell_aux_set', 'flux_shell_aux_set', 'get/set auxiliary handle data', [author], 3),
    ('man3/flux_shell_current_task', 'flux_shell_task_first', 'Get and iterate over shell tasks', [author], 3),
    ('man3/flux_shell_current_task', 'flux_shell_task_next', 'Get and iterate over shell tasks', [author], 3),
    ('man3/flux_shell_current_task', 'flux_shell_current_task', 'Get and iterate over shell tasks', [author], 3),
    ('man3/flux_shell_get_flux', 'flux_shell_get_flux', 'Get a flux_t\* object from flux shell handle', [author], 3),
    ('man3/flux_shell_get_info', 'flux_shell_info_unpack', 'Manage shell info and rank info', [author], 3),
    ('man3/flux_shell_get_info', 'flux_shell_get_rank_info', 'Manage shell info and rank info', [author], 3),
    ('man3/flux_shell_get_info', 'flux_shell_rank_info_unpack', 'Manage shell info and rank info', [author], 3),
    ('man3/flux_shell_get_info', 'flux_shell_get_info', 'Manage shell info and rank info', [author], 3),
    ('man3/flux_shell_getenv', 'flux_shell_get_environ', 'Get and set global environment variables', [author], 3),
    ('man3/flux_shell_getenv', 'flux_shell_setenvf', 'Get and set global environment variables', [author], 3),
    ('man3/flux_shell_getenv', 'flux_shell_unsetenv', 'Get and set global environment variables', [author], 3),
    ('man3/flux_shell_getenv', 'flux_shell_getenv', 'Get and set global environment variables', [author], 3),
    ('man3/flux_shell_getopt', 'flux_shell_getopt_unpack', 'Get and set shell options', [author], 3),
    ('man3/flux_shell_getopt', 'flux_shell_setopt', 'Get and set shell options', [author], 3),
    ('man3/flux_shell_getopt', 'flux_shell_setopt_pack', 'Get and set shell options', [author], 3),
    ('man3/flux_shell_getopt', 'flux_shell_getopt', 'Get and set shell options', [author], 3),
    ('man3/flux_shell_killall', 'flux_shell_killall', 'Send the specified signal to all processes in the shell', [author], 3),
    ('man3/flux_shell_log', 'flux_shell_err', 'Log shell plugin messages to registered shell loggers', [author], 3),
    ('man3/flux_shell_log', 'flux_shell_fatal', 'Log shell plugin messages to registered shell loggers', [author], 3),
    ('man3/flux_shell_log', 'flux_shell_log_setlevel', 'Log shell plugin messages to registered shell loggers', [author], 3),
    ('man3/flux_shell_log', 'flux_shell_log', 'Log shell plugin messages to registered shell loggers', [author], 3),
    ('man3/flux_shell_plugstack_call', 'flux_shell_plugstack_call', 'Calls the function referenced by topic.', [author], 3),
    ('man3/flux_shell_rpc_pack', 'flux_shell_rpc_pack', 'perform an rpc to a running flux shell using Jansson style pack arguments', [author], 3),
    ('man3/flux_shell_service_register', 'flux_shell_service_register', 'Register a service handler for \`method\` in the shell.', [author], 3),
    ('man3/flux_shell_task_channel_subscribe', 'flux_shell_task_channel_subscribe', 'Call \`cb\` when \`name\` is ready for reading.', [author], 3),
    ('man3/flux_shell_task_get_info', 'flux_shell_task_info_unpack', 'interfaces for fetching task info', [author], 3),
    ('man3/flux_shell_task_get_info', 'flux_shell_task_get_info', 'interfaces for fetching task info', [author], 3),
    ('man3/flux_shell_task_subprocess', 'flux_shell_task_cmd', 'return the subprocess and cmd structure of a shell task, respectively', [author], 3),
    ('man3/flux_shell_task_subprocess', 'flux_shell_task_subprocess', 'return the subprocess and cmd structure of a shell task, respectively', [author], 3),
    ('man3/flux_signal_watcher_create', 'flux_signal_watcher_get_signum', 'create signal watcher', [author], 3),
    ('man3/flux_signal_watcher_create', 'flux_signal_watcher_create', 'create signal watcher', [author], 3),
    ('man3/flux_stat_watcher_create', 'flux_stat_watcher_get_rstat', 'create stat watcher', [author], 3),
    ('man3/flux_stat_watcher_create', 'flux_stat_watcher_create', 'create stat watcher', [author], 3),
    ('man3/flux_timer_watcher_create', 'flux_timer_watcher_reset', 'set/reset a timer', [author], 3),
    ('man3/flux_timer_watcher_create', 'flux_timer_watcher_create', 'set/reset a timer', [author], 3),
    ('man3/flux_watcher_start', 'flux_watcher_stop', 'start/stop/destroy/query reactor watcher', [author], 3),
    ('man3/flux_watcher_start', 'flux_watcher_destroy', 'start/stop/destroy/query reactor watcher', [author], 3),
    ('man3/flux_watcher_start', 'flux_watcher_next_wakeup', 'start/stop/destroy/query reactor watcher', [author], 3),
    ('man3/flux_watcher_start', 'flux_watcher_start', 'start/stop/destroy/query reactor watcher', [author], 3),
    ('man3/flux_zmq_watcher_create', 'flux_zmq_watcher_get_zsock', 'create ZeroMQ watcher', [author], 3),
    ('man3/flux_zmq_watcher_create', 'flux_zmq_watcher_create', 'create ZeroMQ watcher', [author], 3),
    ('man3/idset_create', 'idset_create', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_create', 'idset_destroy', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_create', 'idset_set', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_create', 'idset_clear', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_create', 'idset_first', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_create', 'idset_next', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_create', 'idset_count', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_create', 'idset_equal', 'Manipulate numerically sorted sets of non-negative integers', [author], 3),
    ('man3/idset_encode','idset_encode', 'Convert idset to string and string to idset', [author], 3),
    ('man3/idset_encode','idset_decode', 'Convert idset to string and string to idset', [author], 3),
    ('man3/idset_encode','idset_ndecode', 'Convert idset to string and string to idset', [author], 3),
    ('man3/flux_jobtap_get_flux','flux_jobtap_get_flux', 'Flux jobtap plugin interfaces', [author], 3),
    ('man3/flux_jobtap_get_flux','flux_jobtap_service_register', 'Flux jobtap plugin interfaces', [author], 3),
    ('man3/flux_jobtap_get_flux','flux_jobtap_reprioritize_all', 'Flux jobtap plugin interfaces', [author], 3),
    ('man3/flux_jobtap_get_flux','flux_jobtap_reprioritize_job', 'Flux jobtap plugin interfaces', [author], 3),
    ('man3/flux_jobtap_get_flux','flux_jobtap_priority_unavail', 'Flux jobtap plugin interfaces', [author], 3),
    ('man3/flux_jobtap_get_flux','flux_jobtap_reject_job', 'Flux jobtap plugin interfaces', [author], 3),
    ('man3/flux_sync_create','flux_sync_create', 'Synchronize on system heartbeat', [author], 3),
    ('man5/flux-config-bootstrap', 'flux-config-bootstrap', 'configure Flux instance bootstrap', [author], 5),
    ('man7/flux-broker-attributes', 'flux-broker-attributes', 'overview Flux broker attributes', [author], 7),
    ('man7/flux-jobtap-plugins', 'flux-jobtap-plugins', 'overview Flux jobtap plugin API', [author], 7),
]

# -- Options for Intersphinx -------------------------------------------------

intersphinx_mapping = {
    "rfc": (
        "https://flux-framework.readthedocs.io/projects/flux-rfc/en/latest/",
        None,
    ),
    "workflow-examples": (
        "https://flux-framework.readthedocs.io/projects/flux-workflow-examples/en/latest/",
        None,
    ),
}
