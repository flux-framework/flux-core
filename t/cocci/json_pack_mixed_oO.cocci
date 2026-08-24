// SPDX-License-Identifier: LGPL-3.0
//
// Forbid mixing 'o' and 'O' specifiers in a single jansson pack format.
//
// In the jansson pack family (json_pack() and the flux *_pack() wrappers
// built on it), 'o' STEALS the reference to its json_t argument while 'O'
// INCREMENTS it.  A format that uses both in one call therefore mixes two
// opposite ownership conventions: some passed values are consumed and some
// are borrowed, and which is which depends on reading the format positions
// against the argument list.  This is error prone for humans and defeats
// json_pack_o_decref.cocci, whose steal-vs-decref analysis deliberately
// gives up on such formats (it can only reason about a call when every
// json_t argument is handled the same way).  Requiring a single convention
// per call -- all 'o' or all 'O' -- keeps both the code and that check
// tractable.  Rewrite a mixed call to use 'O' throughout (incref) and
// json_decref() each retained value on all paths.
//
// The format string is matched by regex: a rule fires when the format
// literal contains both a lowercase 'o' and an uppercase 'O', in either
// order.  The Str-syntax alternation "o.*O\|O.*o" covers both.
//
// The format metavariable is pinned to its argument position in each
// function so that the regex tests the format string itself and not a key
// string (object keys are separate 's' arguments and may contain the
// letters 'o' and 'O', e.g. "coordO").  Functions are grouped below by how
// many arguments precede the format.  The set matches json_pack_o_decref.cocci;
// the va_list vpack variants take their values through a va_list and are
// likewise out of scope here.

// ---- format is the 1st argument ----

@@
constant char []fmt =~ "o.*O\|O.*o";
identifier packfn = { json_pack, flux_conf_pack };
@@
* packfn(fmt, ...)

// ---- format is the 2nd argument ----

@@
constant char []fmt =~ "o.*O\|O.*o";
identifier packfn =
  { flux_msg_pack, flux_event_pack, flux_jobtap_jobspec_update_pack };
expression a1;
@@
* packfn(a1, fmt, ...)

// ---- format is the 3rd argument ----

@@
constant char []fmt =~ "o.*O\|O.*o";
identifier packfn =
  { json_pack_ex, flux_respond_pack, flux_plugin_arg_pack,
    flux_shell_setopt_pack, flux_jobtap_jobspec_update_id_pack };
expression a1, a2;
@@
* packfn(a1, a2, fmt, ...)

// ---- format is the 4th argument ----

@@
constant char []fmt =~ "o.*O\|O.*o";
identifier packfn =
  { flux_event_publish_pack, flux_kvs_txn_pack, flux_jobtap_event_post_pack };
expression a1, a2, a3;
@@
* packfn(a1, a2, a3, fmt, ...)

// ---- format is the 5th argument ----

@@
constant char []fmt =~ "o.*O\|O.*o";
identifier packfn = { flux_rpc_pack, flux_shell_rpc_pack };
expression a1, a2, a3, a4;
@@
* packfn(a1, a2, a3, a4, fmt, ...)
