/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <unistd.h>
#include <errno.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "verify_config.h"

struct rlist_verify_conf_test {
    const char *description;
    const char *config;
    int rc;
    const char *error;
    enum rlist_verify_mode hostname_mode;
    enum rlist_verify_mode core_mode;
    enum rlist_verify_mode gpu_mode;
};

struct rlist_verify_conf_test verify_conf_tests[] = {
    // NULL/empty cases - all default to strict
    {
        "NULL config defaults to strict",
        NULL,
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
    },
    {
        "empty object defaults to strict",
        "{}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
    },
    // Default only
    {
        "default=strict applies to all",
        "{\"default\":\"strict\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
    },
    {
        "default=allow-extra applies to all",
        "{\"default\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_EXTRA,
    },
    {
        "default=allow-missing applies to all",
        "{\"default\":\"allow-missing\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_MISSING,
        RLIST_VERIFY_ALLOW_MISSING,
        RLIST_VERIFY_ALLOW_MISSING,
    },
    {
        "default=ignore applies to all",
        "{\"default\":\"ignore\"}",
        0,
        NULL,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_IGNORE,
    },
    // Individual resource overrides
    {
        "hostname=ignore overrides default",
        "{\"hostname\":\"ignore\"}",
        0,
        NULL,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
    },
    {
        "hostname=allow-extra",
        "{\"hostname\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
    },
    {
        "core=allow-extra",
        "{\"core\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_STRICT,
    },
    {
        "core=strict",
        "{\"core\":\"strict\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
    },
    {
        "gpu=ignore",
        "{\"gpu\":\"ignore\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_IGNORE,
    },
    {
        "gpu=allow-missing",
        "{\"gpu\":\"allow-missing\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_ALLOW_MISSING,
    },
    {
        "core=allow-missing",
        "{\"core\":\"allow-missing\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_ALLOW_MISSING,
        RLIST_VERIFY_STRICT,
    },
    // Multiple overrides
    {
        "hostname=ignore, core=allow-extra",
        "{\"hostname\":\"ignore\",\"core\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_STRICT,
    },
    {
        "core=ignore, gpu=allow-extra",
        "{\"core\":\"ignore\",\"gpu\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_ALLOW_EXTRA,
    },
    {
        "all three resources with different modes",
        "{\"hostname\":\"allow-missing\","
        "\"core\":\"allow-extra\",\"gpu\":\"ignore\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_MISSING,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_IGNORE,
    },
    // Default with overrides
    {
        "default=allow-extra with gpu=ignore override",
        "{\"default\":\"allow-extra\",\"gpu\":\"ignore\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_IGNORE,
    },
    {
        "default=ignore with hostname=strict override",
        "{\"default\":\"ignore\",\"hostname\":\"strict\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_IGNORE,
    },
    {
        "default with multiple overrides",
        "{\"default\":\"allow-extra\","
        "\"core\":\"strict\",\"gpu\":\"allow-missing\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_ALLOW_MISSING,
    },
    {
        "default=ignore with selective allow-extra",
        "{\"default\":\"ignore\","
        "\"core\":\"allow-extra\",\"gpu\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_EXTRA,
    },
    // Common use cases
    {
        "common: allow extra resources globally",
        "{\"default\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_EXTRA,
    },
    {
        "common: ignore GPU verification (detection issues)",
        "{\"gpu\":\"ignore\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_IGNORE,
    },
    {
        "common: allow extra cores, strict GPUs",
        "{\"core\":\"allow-extra\",\"gpu\":\"strict\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_STRICT,
    },
    {
        "common: boolean instead of table: true == strict",
        "true",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_STRICT,
    },
    {
        "common: boolean instead of table: false == ignore",
        "false",
        0,
        NULL,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_IGNORE,
    },
    // Error cases - invalid mode
    {
        "error: invalid mode string",
        "{\"default\":\"invalid\"}",
        -1,
        "unknown verify mode 'invalid'",
        0, 0, 0,
    },
    {
        "error: bad mode for core",
        "{\"core\":\"bad-mode\"}",
        -1,
        "unknown verify mode 'bad-mode'",
        0, 0, 0,
    },
    {
        "error: typo in mode",
        "{\"hostname\":\"typo\"}",
        -1,
        "unknown verify mode 'typo'",
        0, 0, 0,
    },
    // Error cases - invalid resource type (including plurals)
    {
        "error: plural 'cores' not supported",
        "{\"cores\":\"strict\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    {
        "error: plural 'gpus' not supported",
        "{\"gpus\":\"ignore\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    {
        "error: unsupported resource 'memory'",
        "{\"memory\":\"strict\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    {
        "error: unsupported resource 'fpga'",
        "{\"fpga\":\"ignore\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    {
        "error: invalid resource name",
        "{\"invalid_resource\":\"allow-extra\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    {
        "error: unsupported resource 'nic'",
        "{\"nic\":\"ignore\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    // Error cases - wrong type
    {
        "error: boolean instead of string for default",
        "{\"default\":true}",
        -1,
        "must be a string",
        0, 0, 0,
    },
    {
        "error: number instead of string",
        "{\"core\":123}",
        -1,
        "must be a string",
        0, 0, 0,
    },
    {
        "error: array instead of string",
        "{\"hostname\":[\"strict\"]}",
        -1,
        "must be a string",
        0, 0, 0,
    },
    {
        "error: null instead of string",
        "{\"gpu\":null}",
        -1,
        "must be a string",
        0, 0, 0,
    },
    {
        "error: object instead of string",
        "{\"core\":{\"mode\":\"strict\"}}",
        -1,
        "must be a string",
        0, 0, 0,
    },
    // Error cases - not an object
    {
        "error: string instead of object",
        "\"strict\"",
        -1,
        "must be a table",
        0, 0, 0,
    },
    {
        "error: array instead of object",
        "[\"core\",\"gpu\"]",
        -1,
        "must be a table",
        0, 0, 0,
    },
    {
        "error: number instead of object",
        "42",
        -1,
        "must be a table",
        0, 0, 0,
    },
    // All modes for each resource type
    {
        "all modes: strict, allow-extra, allow-missing",
        "{\"hostname\":\"strict\","
        "\"core\":\"allow-extra\",\"gpu\":\"allow-missing\"}",
        0,
        NULL,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_MISSING,
    },
    {
        "all modes: allow-extra, allow-missing, ignore",
        "{\"hostname\":\"allow-extra\","
        "\"core\":\"allow-missing\",\"gpu\":\"ignore\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_EXTRA,
        RLIST_VERIFY_ALLOW_MISSING,
        RLIST_VERIFY_IGNORE,
    },
    {
        "all modes: allow-missing, ignore, strict",
        "{\"hostname\":\"allow-missing\","
        "\"core\":\"ignore\",\"gpu\":\"strict\"}",
        0,
        NULL,
        RLIST_VERIFY_ALLOW_MISSING,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_STRICT,
    },
    {
        "all modes: ignore, strict, allow-extra",
        "{\"hostname\":\"ignore\","
        "\"core\":\"strict\",\"gpu\":\"allow-extra\"}",
        0,
        NULL,
        RLIST_VERIFY_IGNORE,
        RLIST_VERIFY_STRICT,
        RLIST_VERIFY_ALLOW_EXTRA,
    },
    // Edge cases - empty string values
    {
        "error: empty mode string for core",
        "{\"core\":\"\"}",
        -1,
        "unknown verify mode ''",
        0, 0, 0,
    },
    {
        "error: empty mode string for default",
        "{\"default\":\"\"}",
        -1,
        "unknown verify mode ''",
        0, 0, 0,
    },
    // Mixed valid/invalid
    {
        "error: valid core with invalid resource",
        "{\"core\":\"strict\",\"invalid\":\"strict\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    {
        "error: valid hostname with invalid mode",
        "{\"hostname\":\"strict\",\"core\":\"bad\"}",
        -1,
        "unknown verify mode 'bad'",
        0, 0, 0,
    },
    {
        "error: valid default with invalid resource",
        "{\"default\":\"allow-extra\",\"bad_resource\":\"ignore\"}",
        -1,
        "unsupported resource type",
        0, 0, 0,
    },
    { NULL, NULL, 0, NULL, 0, 0, 0 },  // Sentinel
};

static void test_rlist_verify_config (void)
{
    struct rlist_verify_conf_test *t = &verify_conf_tests[0];
    flux_error_t error;

    while (t->description) {
        struct rlist_verify_config *config = NULL;
        json_t *verify_obj = NULL;
        enum rlist_verify_mode hostname_mode;
        enum rlist_verify_mode core_mode;
        enum rlist_verify_mode gpu_mode;

        // Parse JSON if provided
        if (t->config) {
            json_error_t json_error;
            verify_obj = json_loads (t->config,
                                    JSON_DECODE_ANY,
                                    &json_error);
            if (!verify_obj && t->rc == 0) {
                BAIL_OUT ("%s: invalid test JSON: %s",
                         t->description,
                         json_error.text);
            }
        }

        // Create config
        config = rlist_verify_config_create (verify_obj, &error);

        // Check result of rlist_verify_config_is_explicit():
        if (config && verify_obj)
            ok (rlist_verify_config_is_explicit (config),
                "rlist_verify_config_is_explicit returns true");
        else
            ok (!rlist_verify_config_is_explicit (config),
                "rlist_verify_config_is_explicit returns false");

        // Check expected return code
        if (t->rc == 0) {
            ok (config != NULL,
                "%s",
                t->description);
            if (!config) {
                diag ("unexpected error: %s", error.text);
                json_decref (verify_obj);
                t++;
                continue;
            }
        }
        else {
            ok (config == NULL,
                "%s",
                t->description);
            if (config) {
                diag ("expected failure but got success");
                rlist_verify_config_destroy (config);
                json_decref (verify_obj);
                t++;
                continue;
            }

            // Check error message
            if (t->error) {
                ok (strstr (error.text, t->error) != NULL,
                    "%s: error contains '%s'",
                    t->description,
                    t->error);
                if (!strstr (error.text, t->error))
                    diag ("got error: %s", error.text);
            }

            json_decref (verify_obj);
            t++;
            continue;
        }

        // For successful cases, verify the modes
        hostname_mode = rlist_verify_config_get_mode (config, "hostname");
        core_mode = rlist_verify_config_get_mode (config, "core");
        gpu_mode = rlist_verify_config_get_mode (config, "gpu");

        ok (hostname_mode == t->hostname_mode,
            "%s: hostname mode = %d (expected %d)",
            t->description,
            hostname_mode,
            t->hostname_mode);

        ok (core_mode == t->core_mode,
            "%s: core mode = %d (expected %d)",
            t->description,
            core_mode,
            t->core_mode);

        ok (gpu_mode == t->gpu_mode,
            "%s: gpu mode = %d (expected %d)",
            t->description,
            gpu_mode,
            t->gpu_mode);

        // Now update with true/false and ensure expected result
        ok (rlist_verify_config_update (config, json_true (), &error) == 0,
            "rlist_verify_config_update (true) works");
        ok (rlist_verify_config_get_mode (config,
                                          "default") == RLIST_VERIFY_STRICT
            && rlist_verify_config_get_mode (config,
                                             "core") == RLIST_VERIFY_STRICT
            && rlist_verify_config_get_mode (config,
                                             "gpu") == RLIST_VERIFY_STRICT,
            "all resources now have strict verification");

        ok (rlist_verify_config_update (config, json_false (), &error) == 0,
            "rlist_verify_config_update (false) works");
        ok (rlist_verify_config_get_mode (config,
                                          "default") == RLIST_VERIFY_IGNORE
            && rlist_verify_config_get_mode (config,
                                             "core") == RLIST_VERIFY_IGNORE
            && rlist_verify_config_get_mode (config,
                                             "gpu") == RLIST_VERIFY_IGNORE,
            "all resources now set to ignore");

        rlist_verify_config_destroy (config);
        json_decref (verify_obj);
        t++;
    }
}

void test_rlist_verify_set (void)
{
    struct rlist_verify_config *config;
    if (!(config = rlist_verify_config_create (NULL, NULL)))
        BAIL_OUT ("rlist_verify_config_create failed!");

    ok (rlist_verify_config_set_mode (NULL, "core", RLIST_VERIFY_STRICT) < 0
        && errno == EINVAL,
        "rlist_verify_config_set_mode fails with NULL config");
    ok (rlist_verify_config_set_mode (config, "foo", RLIST_VERIFY_STRICT) < 0
        && errno == EINVAL,
        "rlist_verify_config_set_mode fails with invalid resource");
    ok (rlist_verify_config_set_mode (config,
                                      "core",
                                      RLIST_VERIFY_IGNORE) == 0,
        "rlist_verify_config_set_mode works for core");
    ok (rlist_verify_config_get_mode (config, "core") == RLIST_VERIFY_IGNORE,
        "rlist_verify_config_get_mode (core) = RLIST_VERIFY_IGNORE");
    rlist_verify_config_destroy (config);
}


int main (int ac, char *av[])
{
    plan (NO_PLAN);
    test_rlist_verify_config ();
    test_rlist_verify_set ();
    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
