/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <errno.h>
#include <string.h>

#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/librlist/rlist.h"
#include "src/common/libutil/jpath.h"
#include "ccan/str/str.h"

#include "src/modules/resource/queue_membership.h"

/* Build a 4 node R with hosts test0-test3, 2 cores each.
 */
static json_t *test_R (void)
{
    json_t *config;
    struct rlist *rl = NULL;
    json_t *R = NULL;

    if (!(config = json_pack ("[{s:s s:s}]",
                              "hosts", "test[0-3]",
                              "cores", "0-1"))
        || !(rl = rlist_from_config (config, NULL))
        || !(R = rlist_to_R (rl)))
        BAIL_OUT ("could not create test R");
    rlist_destroy (rl);
    json_decref (config);
    return R;
}

/* Set property 'name' on 'targets' in 'R', as an admin would with
 * [resource.config] properties or flux-R(1) set-property.
 */
static void R_set_property (json_t *R, const char *name, const char *targets)
{
    struct rlist *rl = NULL;
    json_t *newR = NULL;
    json_t *props;

    if (!(rl = rlist_from_json (R, NULL))
        || rlist_add_property (rl, NULL, name, targets) < 0
        || !(newR = rlist_to_R (rl))
        || !(props = jpath_get (newR, "execution.properties"))
        || jpath_set (R, "execution.properties", props) < 0)
        BAIL_OUT ("could not set property %s on R", name);
    json_decref (newR);
    rlist_destroy (rl);
}

/* Return R's execution.properties as a compact string for diagnostics,
 * or "(none)" if the key is absent. Caller frees 'freeme' if non-NULL.
 */
static const char *R_properties_str (json_t *R, char **freeme)
{
    json_t *props = jpath_get (R, "execution.properties");

    *freeme = props ? json_dumps (props, JSON_COMPACT | JSON_SORT_KEYS) : NULL;
    return *freeme ? *freeme : "(none)";
}

/* ok() that R's property 'name' covers exactly idset string 'targets',
 * or that it is absent if 'targets' is NULL.
 */
static void check_property (json_t *R,
                            const char *name,
                            const char *targets,
                            const char *msg)
{
    json_t *props = jpath_get (R, "execution.properties");
    json_t *val = props ? json_object_get (props, name) : NULL;

    if (!targets)
        ok (val == NULL, "%s", msg);
    else
        ok (val != NULL
            && json_is_string (val)
            && streq (json_string_value (val), targets),
            "%s", msg);
}

static void apply_ok (json_t *R, flux_conf_t *conf, const char *msg)
{
    flux_error_t error;
    char *freeme;

    ok (queue_membership_apply (R, conf, &error) == 0, "%s", msg);
    diag ("R properties: %s", R_properties_str (R, &freeme));
    free (freeme);
}

/* No [queues] table at all: R is untouched.
 */
static void test_no_queues (void)
{
    json_t *R = test_R ();
    flux_conf_t *conf = flux_conf_create ();

    if (!conf)
        BAIL_OUT ("flux_conf_create failed");
    apply_ok (R, conf, "apply with no [queues] works");
    check_property (R, "+batch", NULL, "no membership property applied");
    ok (jpath_get (R, "execution.properties") == NULL,
        "execution.properties is absent");

    flux_conf_decref (conf);
    json_decref (R);
}

/* A queue with no 'hosts' gets no membership property.
 */
static void test_bare_queue (void)
{
    json_t *R = test_R ();
    flux_conf_t *conf = flux_conf_pack ("{s:{s:{}}}", "queues", "batch");

    if (!conf)
        BAIL_OUT ("flux_conf_pack failed");
    apply_ok (R, conf, "apply with a bare queue works");
    check_property (R, "+batch", NULL, "bare queue applies no property");

    flux_conf_decref (conf);
    json_decref (R);
}

/* 'hosts' resolves to the named ranks, and "all" to every rank.
 */
static void test_hosts (void)
{
    json_t *R = test_R ();
    flux_conf_t *conf;

    if (!(conf = flux_conf_pack ("{s:{s:{s:s} s:{s:s} s:{s:s} s:{s:s}}}",
                                 "queues",
                                   "batch", "hosts", "test[0-1]",
                                   "debug", "hosts", "test[2-3]",
                                   "allq", "hosts", "all",
                                   "one", "hosts", "test2")))
        BAIL_OUT ("flux_conf_pack failed");
    apply_ok (R, conf, "apply with hosts queues works");
    check_property (R, "+batch", "0-1", "+batch covers the named hosts");
    check_property (R, "+debug", "2-3", "+debug covers the named hosts");
    check_property (R, "+allq", "0-3", "hosts=all covers every rank");
    check_property (R, "+one", "2", "a single host covers a single rank");

    flux_conf_decref (conf);
    json_decref (R);
}

/* An unknown host, or a hostlist resolving to nothing, is a fatal error
 * and leaves R untouched.
 */
static void test_bad_hosts (void)
{
    json_t *R = test_R ();
    flux_conf_t *conf;
    flux_error_t error;

    if (!(conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                 "queues",
                                   "batch", "hosts", "nosuchhost")))
        BAIL_OUT ("flux_conf_pack failed");
    ok (queue_membership_apply (R, conf, &error) < 0,
        "apply with an unknown host fails");
    diag ("error: %s", error.text);
    ok (jpath_get (R, "execution.properties") == NULL,
        "R is untouched after an unknown host");
    flux_conf_decref (conf);

    /* A failure partway through must not leave the earlier queue's
     * property spliced into R.
     */
    if (!(conf = flux_conf_pack ("{s:{s:{s:s} s:{s:s}}}",
                                 "queues",
                                   "aaa", "hosts", "test[0-1]",
                                   "zzz", "hosts", "nosuchhost")))
        BAIL_OUT ("flux_conf_pack failed");
    ok (queue_membership_apply (R, conf, &error) < 0,
        "apply fails when a later queue has a bad hostlist");
    diag ("error: %s", error.text);
    ok (jpath_get (R, "execution.properties") == NULL,
        "R is untouched when one of several queues fails");

    flux_conf_decref (conf);
    json_decref (R);
}

/* Each module load derives from a fresh R, so the membership always
 * reflects the current configuration. Applying the same config twice is
 * stable, and a changed 'hosts' yields only the new ranks.
 */
static void test_reload (void)
{
    json_t *R = test_R ();
    json_t *R2 = test_R ();
    flux_conf_t *conf;
    flux_conf_t *conf2;

    if (!(conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                 "queues",
                                   "batch", "hosts", "test[0-1]")))
        BAIL_OUT ("flux_conf_pack failed");
    if (!(conf2 = flux_conf_pack ("{s:{s:{s:s}}}",
                                  "queues",
                                    "batch", "hosts", "test[2-3]")))
        BAIL_OUT ("flux_conf_pack failed");

    apply_ok (R, conf, "apply works");
    check_property (R, "+batch", "0-1", "+batch covers the first hosts");
    apply_ok (R, conf, "re-apply of the same config works");
    check_property (R, "+batch", "0-1", "+batch is unchanged");

    apply_ok (R2, conf2, "apply of a changed hosts works");
    check_property (R2, "+batch", "2-3", "+batch covers only the new hosts");

    flux_conf_decref (conf);
    flux_conf_decref (conf2);
    json_decref (R);
    json_decref (R2);
}

/* A queue that stops declaring 'hosts' loses its membership property, and
 * dropping the last one removes execution.properties entirely.
 */
static void test_drop_hosts (void)
{
    json_t *R = test_R ();
    json_t *R2 = test_R ();
    flux_conf_t *conf;
    flux_conf_t *bare;

    if (!(conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                 "queues",
                                   "batch", "hosts", "test[0-1]")))
        BAIL_OUT ("flux_conf_pack failed");
    if (!(bare = flux_conf_pack ("{s:{s:{}}}", "queues", "batch")))
        BAIL_OUT ("flux_conf_pack failed");

    apply_ok (R, conf, "apply works");
    check_property (R, "+batch", "0-1", "+batch is applied");
    apply_ok (R2, bare, "apply after dropping hosts works");
    check_property (R2, "+batch", NULL, "+batch is not applied");
    ok (jpath_get (R2, "execution.properties") == NULL,
        "execution.properties is absent with no membership property");

    flux_conf_decref (conf);
    flux_conf_decref (bare);
    json_decref (R);
    json_decref (R2);
}

/* A queue deleted from the config outright leaves no membership property,
 * so temporary queues do not accumulate across reloads.
 */
static void test_deleted_queue (void)
{
    json_t *R = test_R ();
    flux_conf_t *conf;
    flux_conf_t *none;

    if (!(conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                 "queues",
                                   "debug", "hosts", "test[2-3]")))
        BAIL_OUT ("flux_conf_pack failed");
    if (!(none = flux_conf_create ()))
        BAIL_OUT ("flux_conf_create failed");

    /* 'batch' existed on a previous load; this load knows only 'debug'. */
    apply_ok (R, conf, "apply with batch deleted from config works");
    check_property (R, "+debug", "2-3", "+debug is applied");
    check_property (R, "+batch", NULL, "+batch of a deleted queue is absent");

    json_decref (R);
    R = test_R ();
    apply_ok (R, none, "apply with every queue deleted works");
    ok (jpath_get (R, "execution.properties") == NULL,
        "no membership properties remain");

    flux_conf_decref (conf);
    flux_conf_decref (none);
    json_decref (R);
}

/* Properties already on R are never removed: this derivation only adds.
 * R arrives carrying whatever its source put there -- [resource.config]
 * properties, the R named by [resource] path, or the set assigned by an
 * enclosing instance -- including instance-local ones, since '+' is a
 * legal property character.
 */
static void test_existing_properties_kept (void)
{
    json_t *R = test_R ();
    flux_conf_t *conf;

    R_set_property (R, "+foo", "0-3");
    R_set_property (R, "gpu", "0-1");

    if (!(conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                 "queues",
                                   "batch", "hosts", "test[0-1]")))
        BAIL_OUT ("flux_conf_pack failed");
    apply_ok (R, conf, "apply over an R with existing properties works");
    check_property (R, "+foo", "0-3",
                    "an instance-local property on R is kept");
    check_property (R, "gpu", "0-1", "an ordinary property is kept");
    check_property (R, "+batch", "0-1", "+batch is applied alongside");

    flux_conf_decref (conf);
    json_decref (R);
}

/* queue_membership_copy() isolates the key apply() modifies, so deriving
 * into the copy leaves the original untouched. Everything else is shared:
 * R's bulk is not duplicated.
 */
static void test_copy (void)
{
    json_t *R = test_R ();
    json_t *cpy;
    flux_conf_t *conf;

    R_set_property (R, "gpu", "0-1");
    if (jpath_set_new (R, "scheduling", json_string ("opaque")) < 0)
        BAIL_OUT ("could not set scheduling key");
    if (!(conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                 "queues",
                                   "batch", "hosts", "test[0-1]")))
        BAIL_OUT ("flux_conf_pack failed");

    if (!(cpy = queue_membership_copy (R)))
        BAIL_OUT ("queue_membership_copy failed");
    ok (json_object_get (cpy, "scheduling")
        == json_object_get (R, "scheduling"),
        "the copy shares R's other keys rather than duplicating them");
    ok (json_object_get (cpy, "execution")
        != json_object_get (R, "execution"),
        "the copy does not share execution, which apply() modifies");

    apply_ok (cpy, conf, "apply to the copy works");
    check_property (cpy, "+batch", "0-1", "+batch is applied to the copy");
    check_property (R, "+batch", NULL, "the original is not modified");
    check_property (R, "gpu", "0-1", "the original keeps its properties");
    check_property (cpy, "gpu", "0-1", "the copy keeps them too");

    json_decref (cpy);
    flux_conf_decref (conf);
    json_decref (R);
}

/* The copy works when R has no properties at all, which is the usual case
 * before any are derived.
 */
static void test_copy_no_properties (void)
{
    json_t *R = test_R ();
    json_t *cpy;
    flux_conf_t *conf;

    if (!(conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                 "queues",
                                   "batch", "hosts", "test[0-1]")))
        BAIL_OUT ("flux_conf_pack failed");
    if (!(cpy = queue_membership_copy (R)))
        BAIL_OUT ("queue_membership_copy failed");
    ok (jpath_get (cpy, "execution.properties") == NULL,
        "a copy of an R with no properties has none either");

    apply_ok (cpy, conf, "apply to the copy works");
    check_property (cpy, "+batch", "0-1", "+batch is applied to the copy");
    ok (jpath_get (R, "execution.properties") == NULL,
        "the original still has no properties");

    json_decref (cpy);
    flux_conf_decref (conf);
    json_decref (R);
}

/* R's other fields survive the properties splice.
 */
static void test_R_preserved (void)
{
    json_t *R = test_R ();
    flux_conf_t *conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                        "queues",
                                          "batch", "hosts", "test[0-1]");
    json_t *o;

    if (!conf)
        BAIL_OUT ("flux_conf_pack failed");
    if (!jpath_set_new (R, "scheduling", json_string ("opaque")))
        BAIL_OUT ("could not set scheduling key");

    apply_ok (R, conf, "apply works");
    check_property (R, "+batch", "0-1", "+batch is applied");
    ok ((o = jpath_get (R, "scheduling")) != NULL
        && json_is_string (o)
        && streq (json_string_value (o), "opaque"),
        "the scheduling key is preserved");
    ok ((o = jpath_get (R, "execution.nodelist")) != NULL,
        "execution.nodelist is preserved");

    flux_conf_decref (conf);
    json_decref (R);
}

/* Malformed R is an error, not a crash.
 */
static void test_bad_R (void)
{
    json_t *R = json_object ();
    flux_conf_t *conf = flux_conf_pack ("{s:{s:{s:s}}}",
                                        "queues",
                                          "batch", "hosts", "test0");
    flux_error_t error;

    if (!R || !conf)
        BAIL_OUT ("could not create test inputs");
    ok (queue_membership_apply (R, conf, &error) < 0 && errno == EINVAL,
        "apply with malformed R fails with EINVAL");
    diag ("error: %s", error.text);

    flux_conf_decref (conf);
    json_decref (R);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_no_queues ();
    test_bare_queue ();
    test_hosts ();
    test_bad_hosts ();
    test_reload ();
    test_drop_hosts ();
    test_deleted_queue ();
    test_existing_properties_kept ();
    test_copy ();
    test_copy_no_properties ();
    test_R_preserved ();
    test_bad_R ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
