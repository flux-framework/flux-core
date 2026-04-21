// SPDX-License-Identifier: LGPL-3.0
//
// Detect double-free of json_t value after json_*_new() failure.
//
// All jansson json_*_new() functions steal the reference to the value
// argument whether they succeed or fail — on failure, jansson calls
// json_decref() on the value internally.  Calling json_decref() on the
// same value in the caller's error path is therefore a double-free.
//
// Covers both forms:
//
//   Simple:
//     if (json_object_set_new(obj, key, val) < 0) { json_decref(val); }
//
//   Compound (double-free when the _new call is reached and fails):
//     if (E || json_object_set_new(obj, key, val) < 0) { json_decref(val); }
//

// ---- json_object_set_new ----

@@
expression obj, key, val;
@@
* if (json_object_set_new(obj, key, val) < 0) {
  ...
* json_decref(val);
  ...
  }

@@
expression obj, key, val, E;
@@
* if (E || json_object_set_new(obj, key, val) < 0) {
  ...
* json_decref(val);
  ...
  }

// ---- json_array_append_new ----

@@
expression arr, val;
@@
* if (json_array_append_new(arr, val) < 0) {
  ...
* json_decref(val);
  ...
  }

@@
expression arr, val, E;
@@
* if (E || json_array_append_new(arr, val) < 0) {
  ...
* json_decref(val);
  ...
  }

// ---- json_array_set_new ----

@@
expression arr, idx, val;
@@
* if (json_array_set_new(arr, idx, val) < 0) {
  ...
* json_decref(val);
  ...
  }

@@
expression arr, idx, val, E;
@@
* if (E || json_array_set_new(arr, idx, val) < 0) {
  ...
* json_decref(val);
  ...
  }

// ---- json_array_insert_new ----

@@
expression arr, idx, val;
@@
* if (json_array_insert_new(arr, idx, val) < 0) {
  ...
* json_decref(val);
  ...
  }

@@
expression arr, idx, val, E;
@@
* if (E || json_array_insert_new(arr, idx, val) < 0) {
  ...
* json_decref(val);
  ...
  }

// ---- json_object_iter_set_new ----

@@
expression obj, iter, val;
@@
* if (json_object_iter_set_new(obj, iter, val) < 0) {
  ...
* json_decref(val);
  ...
  }

@@
expression obj, iter, val, E;
@@
* if (E || json_object_iter_set_new(obj, iter, val) < 0) {
  ...
* json_decref(val);
  ...
  }
