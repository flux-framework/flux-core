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

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "rhwloc_map.h"

/*  Minimal x86 topology: 1 NUMA node, 1 socket, 2 cores (4 PUs),
 *  1 PCI bridge with 2 CUDA GPU children at known PCI addresses.
 *
 *  Core 0: PUs 0,1 → cpus "0-1"
 *  Core 1: PUs 2,3 → cpus "2-3"
 *  Both cores in NUMA node 0 → mems "0"
 *  GPU 0: PCI 0000:01:00.0
 *  GPU 1: PCI 0000:02:00.0
 */
static const char xml[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\"\
 cpuset=\"0x0000000f\" complete_cpuset=\"0x0000000f\"\
 online_cpuset=\"0x0000000f\" allowed_cpuset=\"0x0000000f\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
    <object type=\"NUMANode\" os_index=\"0\"\
 cpuset=\"0x0000000f\" complete_cpuset=\"0x0000000f\"\
 online_cpuset=\"0x0000000f\" allowed_cpuset=\"0x0000000f\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\" local_memory=\"8589934592\">\n\
      <object type=\"Package\" os_index=\"0\"\
 cpuset=\"0x0000000f\" complete_cpuset=\"0x0000000f\"\
 online_cpuset=\"0x0000000f\" allowed_cpuset=\"0x0000000f\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"Core\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
          <object type=\"PU\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
        <object type=\"Core\" os_index=\"1\"\
 cpuset=\"0x0000000c\" complete_cpuset=\"0x0000000c\"\
 online_cpuset=\"0x0000000c\" allowed_cpuset=\"0x0000000c\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"2\"\
 cpuset=\"0x00000004\" complete_cpuset=\"0x00000004\"\
 online_cpuset=\"0x00000004\" allowed_cpuset=\"0x00000004\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
          <object type=\"PU\" os_index=\"3\"\
 cpuset=\"0x00000008\" complete_cpuset=\"0x00000008\"\
 online_cpuset=\"0x00000008\" allowed_cpuset=\"0x00000008\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
    <object type=\"Bridge\" os_index=\"0\" bridge_type=\"0-1\" depth=\"0\"\
 bridge_pci=\"0000:[00-02]\">\n\
      <object type=\"PCIDev\" os_index=\"4096\" name=\"Test GPU 0\"\
 pci_busid=\"0000:01:00.0\" pci_type=\"0302 [10de:1234] [10de:0000] a1\"\
 pci_link_speed=\"0.000000\">\n\
        <object type=\"OSDev\" name=\"cuda0\" osdev_type=\"5\">\n\
          <info name=\"CoProcType\" value=\"CUDA\"/>\n\
          <info name=\"Backend\" value=\"CUDA\"/>\n\
        </object>\n\
      </object>\n\
      <object type=\"PCIDev\" os_index=\"8192\" name=\"Test GPU 1\"\
 pci_busid=\"0000:02:00.0\" pci_type=\"0302 [10de:1234] [10de:0000] a1\"\
 pci_link_speed=\"0.000000\">\n\
        <object type=\"OSDev\" name=\"cuda1\" osdev_type=\"5\">\n\
          <info name=\"CoProcType\" value=\"CUDA\"/>\n\
          <info name=\"Backend\" value=\"CUDA\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
  </object>\n\
</topology>\n";

void test_create (void)
{
    rhwloc_map_t *m;

    m = rhwloc_map_create ("not xml");
    ok (m == NULL,
        "rhwloc_map_create with bad XML returns NULL");

    m = rhwloc_map_create (xml);
    ok (m != NULL,
        "rhwloc_map_create with valid XML works");
    rhwloc_map_destroy (m);

    rhwloc_map_destroy (NULL);
    pass ("rhwloc_map_destroy(NULL) is safe");
}

void test_cores (void)
{
    rhwloc_map_t *m;
    char *cpus = NULL;
    char *mems = NULL;

    m = rhwloc_map_create (xml);
    if (!m)
        BAIL_OUT ("rhwloc_map_create failed");

    /* NULL inputs */
    ok (rhwloc_map_cores (NULL, "0", &cpus, &mems) == -1 && errno == EINVAL,
        "rhwloc_map_cores m=NULL returns -1, EINVAL");
    ok (rhwloc_map_cores (m, NULL, &cpus, &mems) == -1 && errno == EINVAL,
        "rhwloc_map_cores cores=NULL returns -1, EINVAL");
    ok (rhwloc_map_cores (m, "0", NULL, &mems) == -1 && errno == EINVAL,
        "rhwloc_map_cores cpus_out=NULL returns -1, EINVAL");
    ok (rhwloc_map_cores (m, "0", &cpus, NULL) == -1 && errno == EINVAL,
        "rhwloc_map_cores mems_out=NULL returns -1, EINVAL");

    /* Core 0: PUs 0,1 in NUMA node 0 */
    ok (rhwloc_map_cores (m, "0", &cpus, &mems) == 0,
        "rhwloc_map_cores cores=0 returns 0");
    ok (cpus != NULL && streq (cpus, "0-1"),
        "rhwloc_map_cores cores=0 cpus=\"0-1\": got \"%s\"",
        cpus ? cpus : "(null)");
    ok (mems != NULL && streq (mems, "0"),
        "rhwloc_map_cores cores=0 mems=\"0\": got \"%s\"",
        mems ? mems : "(null)");
    free (cpus); cpus = NULL;
    free (mems); mems = NULL;

    /* Core 1: PUs 2,3 in NUMA node 0 */
    ok (rhwloc_map_cores (m, "1", &cpus, &mems) == 0,
        "rhwloc_map_cores cores=1 returns 0");
    ok (cpus != NULL && streq (cpus, "2-3"),
        "rhwloc_map_cores cores=1 cpus=\"2-3\": got \"%s\"",
        cpus ? cpus : "(null)");
    ok (mems != NULL && streq (mems, "0"),
        "rhwloc_map_cores cores=1 mems=\"0\": got \"%s\"",
        mems ? mems : "(null)");
    free (cpus); cpus = NULL;
    free (mems); mems = NULL;

    /* Cores 0-1: all PUs, all in NUMA node 0 */
    ok (rhwloc_map_cores (m, "0-1", &cpus, &mems) == 0,
        "rhwloc_map_cores cores=0-1 returns 0");
    ok (cpus != NULL && streq (cpus, "0-3"),
        "rhwloc_map_cores cores=0-1 cpus=\"0-3\": got \"%s\"",
        cpus ? cpus : "(null)");
    ok (mems != NULL && streq (mems, "0"),
        "rhwloc_map_cores cores=0-1 mems=\"0\": got \"%s\"",
        mems ? mems : "(null)");
    free (cpus); cpus = NULL;
    free (mems); mems = NULL;

    rhwloc_map_destroy (m);
}

void test_gpu_pci_addrs (void)
{
    rhwloc_map_t *m;
    char **addrs;

    m = rhwloc_map_create (xml);
    if (!m)
        BAIL_OUT ("rhwloc_map_create failed");

    /* NULL inputs */
    ok (rhwloc_map_gpu_pci_addrs (NULL, "0") == NULL && errno == EINVAL,
        "rhwloc_map_gpu_pci_addrs m=NULL returns NULL, EINVAL");
    ok (rhwloc_map_gpu_pci_addrs (m, NULL) == NULL && errno == EINVAL,
        "rhwloc_map_gpu_pci_addrs gpus=NULL returns NULL, EINVAL");

    /* GPU 0 */
    addrs = rhwloc_map_gpu_pci_addrs (m, "0");
    ok (addrs != NULL,
        "rhwloc_map_gpu_pci_addrs gpus=0 returns non-NULL");
    ok (addrs != NULL && addrs[0] != NULL
        && streq (addrs[0], "0000:01:00.0"),
        "rhwloc_map_gpu_pci_addrs gpus=0 addrs[0]=\"0000:01:00.0\": got \"%s\"",
        addrs && addrs[0] ? addrs[0] : "(null)");
    ok (addrs != NULL && addrs[1] == NULL,
        "rhwloc_map_gpu_pci_addrs gpus=0 result is NULL-terminated");
    rhwloc_map_strv_free (addrs);

    /* GPU 1 */
    addrs = rhwloc_map_gpu_pci_addrs (m, "1");
    ok (addrs != NULL,
        "rhwloc_map_gpu_pci_addrs gpus=1 returns non-NULL");
    ok (addrs != NULL && addrs[0] != NULL
        && streq (addrs[0], "0000:02:00.0"),
        "rhwloc_map_gpu_pci_addrs gpus=1 addrs[0]=\"0000:02:00.0\": got \"%s\"",
        addrs && addrs[0] ? addrs[0] : "(null)");
    rhwloc_map_strv_free (addrs);

    /* GPUs 0-1 */
    addrs = rhwloc_map_gpu_pci_addrs (m, "0-1");
    ok (addrs != NULL,
        "rhwloc_map_gpu_pci_addrs gpus=0-1 returns non-NULL");
    ok (addrs != NULL && addrs[0] != NULL
        && streq (addrs[0], "0000:01:00.0"),
        "rhwloc_map_gpu_pci_addrs gpus=0-1 addrs[0]=\"0000:01:00.0\"");
    ok (addrs != NULL && addrs[1] != NULL
        && streq (addrs[1], "0000:02:00.0"),
        "rhwloc_map_gpu_pci_addrs gpus=0-1 addrs[1]=\"0000:02:00.0\"");
    ok (addrs != NULL && addrs[2] == NULL,
        "rhwloc_map_gpu_pci_addrs gpus=0-1 result is NULL-terminated");
    rhwloc_map_strv_free (addrs);

    /* Out-of-range GPU */
    addrs = rhwloc_map_gpu_pci_addrs (m, "2");
    ok (addrs == NULL,
        "rhwloc_map_gpu_pci_addrs out-of-range GPU returns NULL");

    rhwloc_map_destroy (m);
}

void test_strv_free (void)
{
    rhwloc_map_strv_free (NULL);
    pass ("rhwloc_map_strv_free(NULL) is safe");
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_create ();
    test_cores ();
    test_gpu_pci_addrs ();
    test_strv_free ();

    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
