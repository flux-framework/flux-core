// SPDX-License-Identifier: LGPL-3.0
//
// Detect double-free of a json_t value stolen by a bare 'o' pack specifier.
//
// In the jansson pack family (json_pack() and the flux *_pack() wrappers
// built on it), the 'o' specifier STEALS the reference to its json_t
// argument, whether the pack succeeds or fails.  On failure jansson still
// consumes (decref's) the value, so a subsequent json_decref() of that same
// value is a double-free on the error path (or a premature free of a
// now-container-owned reference on the success path).  Once a value has
// been handed to a pack function via 'o', the caller must not decref it
// again unless it first reassigns or re-increfs it.  ('O', by contrast,
// increments the refcount, so the caller retains and must free its own
// reference -- that decref is correct and is NOT flagged.)
//
// The format string is matched by regex: a rule fires only when the format
// literal contains a lowercase 'o' and no uppercase 'O'.  With no 'O'
// present, every json_t passed is stolen, so a decref of any passed value
// is unambiguously wrong -- there is no borrowed 'O' value to confuse it
// with.  Formats mixing 'o' and 'O', and non-literal (computed) formats,
// are intentionally not handled here.  ('o'/'O' are the only two specifiers
// that consume a json_t; scalar args like 's'/'i'/'f' are never decref'd,
// so their presence in the format is harmless.)
//
// The format metavariable is pinned to its argument position in each
// function so that the regex tests the format string itself and not a key
// string (object keys are separate 's' arguments and may contain the
// letter 'o', e.g. "mods").  Functions are grouped below by how many
// arguments precede the format.
//
// Like json_set_new_decref.cocci, this matches on data flow, so it catches
// the decref regardless of how control reaches it (direct, via goto to a
// shared error label, or via ERRNO_SAFE_WRAP() at such a label).  The
// "when !=" constraints suppress the match when the value is reassigned or
// re-increfed between the steal and the decref.

// ---- format is the 1st argument ----

@@
constant char []fmt =~ "^[^O]*o[^O]*$";
identifier packfn = { json_pack, flux_conf_pack };
identifier decref = json_decref;
expression val, E;
@@
  packfn(fmt, ..., val, ...)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- format is the 2nd argument ----

@@
constant char []fmt =~ "^[^O]*o[^O]*$";
identifier packfn =
  { flux_msg_pack, flux_event_pack, flux_jobtap_jobspec_update_pack };
identifier decref = json_decref;
expression a1, val, E;
@@
  packfn(a1, fmt, ..., val, ...)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- format is the 3rd argument ----

@@
constant char []fmt =~ "^[^O]*o[^O]*$";
identifier packfn =
  { json_pack_ex, flux_respond_pack, flux_plugin_arg_pack,
    flux_shell_setopt_pack, flux_jobtap_jobspec_update_id_pack };
identifier decref = json_decref;
expression a1, a2, val, E;
@@
  packfn(a1, a2, fmt, ..., val, ...)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- format is the 4th argument ----

@@
constant char []fmt =~ "^[^O]*o[^O]*$";
identifier packfn =
  { flux_event_publish_pack, flux_kvs_txn_pack, flux_jobtap_event_post_pack };
identifier decref = json_decref;
expression a1, a2, a3, val, E;
@@
  packfn(a1, a2, a3, fmt, ..., val, ...)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- format is the 5th argument ----

@@
constant char []fmt =~ "^[^O]*o[^O]*$";
identifier packfn = { flux_rpc_pack, flux_shell_rpc_pack };
identifier decref = json_decref;
expression a1, a2, a3, a4, val, E;
@@
  packfn(a1, a2, a3, a4, fmt, ..., val, ...)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)
