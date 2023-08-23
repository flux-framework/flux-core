/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/read_all.h"
#include "src/modules/job-list/job_data.h"
#include "ccan/str/str.h"

struct test_jobspec_corner_case {
    const char *filename;
    int expected;
} jobspec_corner_case_tests[] = {
    { TEST_SRCDIR "/jobspec/invalid_json.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/missing_attributes.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_attributes_system_job.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_attributes_system_missing_duration.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/missing_tasks.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_tasks_array.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_tasks_missing_command.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_command_array.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_command_string.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_per_resource_missing_type.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/missing_version.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_version.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/missing_resources.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_resources.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_resources_missing_type.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_resources_invalid_type.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_resources_missing_count.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_resources_invalid_count.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_resources_noslots.jobspec", -1 },
    { TEST_SRCDIR "/jobspec/invalid_resources_nocores.jobspec", -1 },
    { NULL, 0 },
};

struct test_jobspec_job_name {
    const char *filename;
    const char *job_name;
} jobspec_job_name_tests[] = {
    { TEST_SRCDIR "/jobspec/1slot.jobspec", "hostname" },
    { TEST_SRCDIR "/jobspec/job_name_alt.jobspec", "altname" },
    { NULL, 0 },
};

struct test_jobspec_cwd {
    const char *filename;
    const char *cwd;
} jobspec_cwd_tests[] = {
    { TEST_SRCDIR "/jobspec/1slot.jobspec", "/tmp/job" },
    { TEST_SRCDIR "/jobspec/cwd_not_specified.jobspec", NULL },
    { NULL, 0 },
};

struct test_jobspec_queue {
    const char *filename;
    const char *queue;
} jobspec_queue_tests[] = {
    { TEST_SRCDIR "/jobspec/1slot.jobspec", NULL },
    { TEST_SRCDIR "/jobspec/queue_specified.jobspec", "batch" },
    { NULL, 0 },
};

struct test_jobspec_duration {
    const char *filename;
    double duration;
} jobspec_duration_tests[] = {
    { TEST_SRCDIR "/jobspec/1slot.jobspec", 0.0 },
    { TEST_SRCDIR "/jobspec/duration_alt.jobspec", 100.0 },
    { NULL, 0 },
};

struct test_R_corner_case {
    const char *filename;
    int expected;
} R_corner_case_tests[] = {
    { TEST_SRCDIR "/R/missing_starttime.R", 0 },
    { TEST_SRCDIR "/R/missing_expiration.R", 0 },
    { TEST_SRCDIR "/R/invalid_json.R", -1 },
    { TEST_SRCDIR "/R/missing_version.R", -1 },
    { TEST_SRCDIR "/R/invalid_version.R", -1 },
    { TEST_SRCDIR "/R/invalid_R_lite.R", -1 },
    { TEST_SRCDIR "/R/missing_nodelist.R", -1 },
    { TEST_SRCDIR "/R/invalid_nodelist.R", -1 },
    { NULL, 0 },
};

struct test_R_ranks {
    const char *filename;
    const char *ranks;
} R_ranks_tests[] = {
    { TEST_SRCDIR "/R/1node_4core.R", "0" },
    { TEST_SRCDIR "/R/4node_4core.R", "[0-3]" },
    { NULL, 0 },
};

struct test_R_nodelist {
    const char *filename;
    const char *nodelist;
} R_nodelist_tests[] = {
    { TEST_SRCDIR "/R/1node_4core.R", "node1" },
    { TEST_SRCDIR "/R/4node_4core.R", "node[1-4]" },
    { NULL, 0 },
};

struct test_nnodes {
    const char *jobspec_filename;
    const char *R_filename;
    int nnodes_after_jobspec;
    int nnodes_after_R;
} nnodes_tests[] = {
    {
        TEST_SRCDIR "/jobspec/1slot.jobspec",
        TEST_SRCDIR "/R/1node_1core.R",
        -1,
        1,
    },
    {
        TEST_SRCDIR "/jobspec/4slot.jobspec",
        TEST_SRCDIR "/R/4node_4core.R",
        -1,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/1node.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        1,
        1,
    },
    {
        TEST_SRCDIR "/jobspec/4node.jobspec",
        TEST_SRCDIR "/R/4node_4core.R",
        4,
        4,
    },
    { NULL, NULL, 0, 0 },
};

struct test_ntasks {
    const char *jobspec_filename;
    const char *R_filename;
    int ntasks_after_jobspec;
    int ntasks_after_R;
} ntasks_tests[] = {
    {
        TEST_SRCDIR "/jobspec/1slot.jobspec",
        TEST_SRCDIR "/R/1node_1core.R",
        1,
        1,
    },
    {
        TEST_SRCDIR "/jobspec/4slot.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        4,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/1node.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        1,
        1,
    },
    {
        TEST_SRCDIR "/jobspec/4node.jobspec",
        TEST_SRCDIR "/R/4node_4core.R",
        4,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/1node_perresourcenode4.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        4,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/4node_perresourcenode4.jobspec",
        TEST_SRCDIR "/R/4node_4core.R",
        16,
        16,
    },
    {
        TEST_SRCDIR "/jobspec/1slot_perresourcecore4.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        4,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/4slot_perresourcecore4.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        16,
        16,
    },
    {
        TEST_SRCDIR "/jobspec/1node_perresourcecore4.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        -1,
        16,
    },
    {
        TEST_SRCDIR "/jobspec/4node_perresourcecore4.jobspec",
        TEST_SRCDIR "/R/4node_4core.R",
        -1,
        64,
    },
    { NULL, NULL, 0, 0 },
};

struct test_ncores {
    const char *jobspec_filename;
    const char *R_filename;
    int ncores_after_jobspec;
    int ncores_after_R;
} ncores_tests[] = {
    {
        TEST_SRCDIR "/jobspec/1slot.jobspec",
        TEST_SRCDIR "/R/1node_1core.R",
        1,
        1,
    },
    {
        TEST_SRCDIR "/jobspec/4slot.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        4,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/1slot_4core.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        4,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/1node.jobspec",
        TEST_SRCDIR "/R/1node_4core.R",
        -1,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/4node.jobspec",
        TEST_SRCDIR "/R/4node_4core.R",
        -1,
        16,
    },
    {
        TEST_SRCDIR "/jobspec/1node_1slot_nonexclusive.jobspec",
        TEST_SRCDIR "/R/1node_1core.R",
        1,
        1,
    },
    {
        TEST_SRCDIR "/jobspec/4node_1slot_nonexclusive.jobspec",
        TEST_SRCDIR "/R/4node_1core.R",
        4,
        4,
    },
    {
        TEST_SRCDIR "/jobspec/4node_4slot_nonexclusive.jobspec",
        TEST_SRCDIR "/R/4node_4core.R",
        16,
        16,
    },
    { NULL, NULL, 0, 0 },
};

static void read_file (const char *filename, void **datap)
{
    int fd;
    ssize_t size;

    if ((fd = open (filename, O_RDONLY)) < 0)
        BAIL_OUT ("failed to open %s", filename);
    /* N.B. read_all() NUL terminates buffer */
    if ((size = read_all (fd, datap)) < 0)
        BAIL_OUT ("failed to read data %s", filename);
    close (fd);
}

static int parse_jobspec (struct job *job, const char *filename)
{
    char *data;
    int ret;

    read_file (filename, (void **)&data);

    ret = job_parse_jobspec_fatal (job, data);

    free (data);
    return ret;
}

static int parse_R (struct job *job, const char *filename)
{
    char *data;
    int ret;

    read_file (filename, (void **)&data);

    ret = job_parse_R_fatal (job, data);

    free (data);
    return ret;
}

static void test_jobspec_corner_case (void)
{
    struct test_jobspec_corner_case *test;

    test = jobspec_corner_case_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        int expected = test->expected;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_jobspec (job, filename);
        ok (ret == expected, "job_parse_jobspec passes on %s", filename);

        job_destroy (job);
        test++;
    }
}

static void test_jobspec_job_name (void)
{
    struct test_jobspec_job_name *test;

    test = jobspec_job_name_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        const char *job_name = test->job_name;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_jobspec (job, filename);
        ok (ret == 0, "job_parse_jobspec parsed %s", filename);
        ok (streq (job_name, job->name),
            "job_parse_jobspec correctly parsed job name %s=%s",
            job_name, job->name);

        job_destroy (job);
        test++;
    }
}

static void test_jobspec_cwd (void)
{
    struct test_jobspec_cwd *test;

    test = jobspec_cwd_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        const char *cwd = test->cwd;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_jobspec (job, filename);
        ok (ret == 0, "job_parse_jobspec parsed %s", filename);
        if (cwd) {
            ok (streq (cwd, job->cwd),
                "job_parse_jobspec correctly parsed job cwd %s=%s",
                cwd, job->cwd);
        }
        else {
            ok (job->cwd == NULL,
                "job_parse_jobspec correctly parsed no job cwd");
        }

        job_destroy (job);
        test++;
    }
}

static void test_jobspec_queue (void)
{
    struct test_jobspec_queue *test;

    test = jobspec_queue_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        const char *queue = test->queue;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_jobspec (job, filename);
        ok (ret == 0, "job_parse_jobspec parsed %s", filename);
        if (queue) {
            ok (streq (queue, job->queue),
                "job_parse_jobspec correctly parsed job queue %s=%s",
                queue, job->queue);
        }
        else {
            ok (job->queue == NULL,
                "job_parse_jobspec correctly parsed no job queue");
        }

        job_destroy (job);
        test++;
    }
}

static void test_jobspec_duration (void)
{
    struct test_jobspec_duration *test;

    test = jobspec_duration_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        double duration = test->duration;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_jobspec (job, filename);
        ok (ret == 0, "job_parse_jobspec parsed %s", filename);
        ok (duration == job->duration,
            "job_parse_jobspec correctly parsed duration %f=%f",
            duration, job->duration);

        job_destroy (job);
        test++;
    }
}

static void test_R_corner_case (void)
{
    struct test_R_corner_case *test;

    test = R_corner_case_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        int expected = test->expected;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_R (job, filename);
        ok (ret == expected, "job_parse_R passes on %s", filename);

        job_destroy (job);
        test++;
    }
}

static void test_R_ranks (void)
{
    struct test_R_ranks *test;

    test = R_ranks_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        const char *ranks = test->ranks;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_R (job, filename);
        ok (ret == 0, "job_parse_R parsed %s", filename);
        ok (streq (ranks, job->ranks),
            "job_parse_jobspec correctly parsed job ranks %s=%s",
            ranks, job->ranks);

        job_destroy (job);
        test++;
    }
}

static void test_R_nodelist (void)
{
    struct test_R_nodelist *test;

    test = R_nodelist_tests;
    while (test->filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *filename = test->filename;
        const char *nodelist = test->nodelist;
        int ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        ret = parse_R (job, filename);
        ok (ret == 0, "job_parse_R parsed %s", filename);
        ok (streq (nodelist, job->nodelist),
            "job_parse_jobspec correctly parsed job nodelist %s=%s",
            nodelist, job->nodelist);

        job_destroy (job);
        test++;
    }
}

static void test_nnodes (void)
{
    struct test_nnodes *test;

    test = nnodes_tests;
    while (test->jobspec_filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *jobspec_filename = test->jobspec_filename;
        const char *R_filename = test->R_filename;
        int nnodes_after_jobspec = test->nnodes_after_jobspec;
        int nnodes_after_R = test->nnodes_after_R;
        int jobspec_ret, R_ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        jobspec_ret = parse_jobspec (job, jobspec_filename);
        ok (jobspec_ret == 0, "job_parse_jobspec parsed %s", jobspec_filename);

        ok (nnodes_after_jobspec == job->nnodes,
            "job_parse_jobspec correctly parsed nnodes %d=%d",
            nnodes_after_jobspec, job->nnodes);

        R_ret = parse_R (job, R_filename);
        ok (R_ret == 0, "job_parse_R parsed %s", R_filename);

        ok (nnodes_after_R == job->nnodes,
            "job_parse_R correctly parsed nnodes %d=%d",
            nnodes_after_R, job->nnodes);

        job_destroy (job);
        test++;
    }
}

static void test_ntasks (void)
{
    struct test_ntasks *test;

    test = ntasks_tests;
    while (test->jobspec_filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *jobspec_filename = test->jobspec_filename;
        const char *R_filename = test->R_filename;
        int ntasks_after_jobspec = test->ntasks_after_jobspec;
        int ntasks_after_R = test->ntasks_after_R;
        int jobspec_ret, R_ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        jobspec_ret = parse_jobspec (job, jobspec_filename);
        ok (jobspec_ret == 0, "job_parse_jobspec parsed %s", jobspec_filename);

        ok (ntasks_after_jobspec == job->ntasks,
            "job_parse_jobspec correctly parsed ntasks %d=%d",
            ntasks_after_jobspec, job->ntasks);

        R_ret = parse_R (job, R_filename);
        ok (R_ret == 0, "job_parse_R parsed %s", R_filename);

        ok (ntasks_after_R == job->ntasks,
            "job_parse_R correctly parsed ntasks %d=%d",
            ntasks_after_R, job->ntasks);

        job_destroy (job);
        test++;
    }
}

static void test_ncores (void)
{
    struct test_ncores *test;

    test = ncores_tests;
    while (test->jobspec_filename) {
        struct job *job = job_create (NULL, FLUX_JOBID_ANY);
        const char *jobspec_filename = test->jobspec_filename;
        const char *R_filename = test->R_filename;
        int ncores_after_jobspec = test->ncores_after_jobspec;
        int ncores_after_R = test->ncores_after_R;
        int jobspec_ret, R_ret;

        if (!job)
            BAIL_OUT ("job_create failed");

        jobspec_ret = parse_jobspec (job, jobspec_filename);
        ok (jobspec_ret == 0, "job_parse_jobspec parsed %s", jobspec_filename);

        ok (ncores_after_jobspec == job->ncores,
            "job_parse_jobspec correctly parsed ncores %d=%d",
            ncores_after_jobspec, job->ncores);

        R_ret = parse_R (job, R_filename);
        ok (R_ret == 0, "job_parse_R parsed %s", R_filename);

        ok (ncores_after_R == job->ncores,
            "job_parse_R correctly parsed ncores %d=%d",
            ncores_after_R, job->ncores);

        job_destroy (job);
        test++;
    }
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_jobspec_corner_case ();
    test_jobspec_job_name ();
    test_jobspec_cwd ();
    test_jobspec_queue ();
    test_jobspec_duration ();
    test_R_corner_case ();
    test_R_ranks ();
    test_R_nodelist ();
    test_nnodes ();
    test_ntasks ();
    test_ncores ();

    done_testing ();
}

/*
 * vi:ts=4 sw=4 expandtab
 */
