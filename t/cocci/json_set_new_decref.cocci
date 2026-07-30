// SPDX-License-Identifier: LGPL-3.0
//
// Detect double-free of json_t value after json_*_new() failure.
//
// All jansson json_*_new() functions steal the reference to the value
// argument whether they succeed or fail — on failure, jansson calls
// json_decref() on the value internally.  Calling json_decref() on the
// same value afterward is therefore wrong: a double-free on the failure
// path, or a premature free of a now-container-owned reference on the
// success path.  Either way, once the value has been passed to a
// json_*_new() function, the caller must not decref it again unless it
// is first reassigned (or re-increfed).
//
// These rules match on data flow rather than on if/block structure, so
// they catch the decref no matter how control reaches it:
//
//   Direct:
//     if (json_object_set_new(obj, key, val) < 0)
//         json_decref(val);
//
//   Via goto to a shared error label:
//     if (json_object_set_new(obj, key, val) < 0)
//         goto error;
//     ...
//   error:
//     json_decref(val);
//
//   Via the ERRNO_SAFE_WRAP() macro at such a label:
//   error:
//     ERRNO_SAFE_WRAP (json_decref, val);
//
// The "when != " constraints suppress the match when the value is
// reassigned or re-increfed between the steal and the decref, since in
// those cases the later decref refers to a reference the caller does
// own.

// ---- json_object_set_new ----

@@
expression obj, key, val, E;
identifier decref = json_decref;
@@
  json_object_set_new(obj, key, val)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- json_array_append_new ----

@@
expression arr, val, E;
identifier decref = json_decref;
@@
  json_array_append_new(arr, val)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- json_array_set_new ----

@@
expression arr, idx, val, E;
identifier decref = json_decref;
@@
  json_array_set_new(arr, idx, val)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- json_array_insert_new ----

@@
expression arr, idx, val, E;
identifier decref = json_decref;
@@
  json_array_insert_new(arr, idx, val)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)

// ---- json_object_iter_set_new ----

@@
expression obj, iter, val, E;
identifier decref = json_decref;
@@
  json_object_iter_set_new(obj, iter, val)
  ... when != val = E
      when != json_incref(val)
(
* decref(val)
|
* ERRNO_SAFE_WRAP(decref, val)
)
