/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/*
 *  Push single json object `o` to local aggregator module via RPC.
 *   Steals the reference to `o`.
 */
flux_future_t *aggregator_push_json (flux_t *h, const char *key, json_t *o);

/*  Fulfill future when aggregate at `key` is "complete", i.e.
 *   count == total. Use aggreate_wait_get_unpack () to unpack final
 *   aggregate kvs value after successful fulfillment.
 */
flux_future_t *aggregate_wait (flux_t *h, const char *key);

/*  Get final aggregate JSON object using Jansson json_unpack() format:
 */
int aggregate_wait_get_unpack (flux_future_t *f, const char *fmt, ...);

/*  Unpack the aggregate fulfilled in `f` into the kvs at path.
 *   Just the aggregate `entries` object is pushed to the new location,
 *   dropping the aggregate context count, total, min, max, etc.
 *
 *  The original aggregate key is removed.
 */
int aggregate_unpack_to_kvs (flux_future_t *f, const char *path);
