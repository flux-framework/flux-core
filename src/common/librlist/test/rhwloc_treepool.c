/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Test TreePool topology generation */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/errprintf.h"
#include "rhwloc.h"
#include "rhwloc_treepool.h"

/* NPS1 single package: 2 cores, 8 GiB */
static const char xml_nps1_1pkg[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
    <info name=\"HostName\" value=\"testhost\"/>\n\
    <object type=\"NUMANode\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\" local_memory=\"8589934592\">\n\
      <object type=\"Package\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"Core\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
        <object type=\"Core\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
  </object>\n\
</topology>\n";

/* NPS1 two packages: 2 sockets, each with 2 cores and 8 GiB */
static const char xml_nps1_2pkg[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\"\
 cpuset=\"0x0000000f\" complete_cpuset=\"0x0000000f\"\
 online_cpuset=\"0x0000000f\" allowed_cpuset=\"0x0000000f\"\
 nodeset=\"0x00000003\" complete_nodeset=\"0x00000003\"\
 allowed_nodeset=\"0x00000003\">\n\
    <info name=\"HostName\" value=\"testhost\"/>\n\
    <object type=\"NUMANode\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\" local_memory=\"8589934592\">\n\
      <object type=\"Package\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"Core\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
        <object type=\"Core\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
    <object type=\"NUMANode\" os_index=\"1\"\
 cpuset=\"0x0000000c\" complete_cpuset=\"0x0000000c\"\
 online_cpuset=\"0x0000000c\" allowed_cpuset=\"0x0000000c\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\" local_memory=\"8589934592\">\n\
      <object type=\"Package\" os_index=\"1\"\
 cpuset=\"0x0000000c\" complete_cpuset=\"0x0000000c\"\
 online_cpuset=\"0x0000000c\" allowed_cpuset=\"0x0000000c\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\">\n\
        <object type=\"Core\" os_index=\"2\"\
 cpuset=\"0x00000004\" complete_cpuset=\"0x00000004\"\
 online_cpuset=\"0x00000004\" allowed_cpuset=\"0x00000004\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\">\n\
          <object type=\"PU\" os_index=\"2\"\
 cpuset=\"0x00000004\" complete_cpuset=\"0x00000004\"\
 online_cpuset=\"0x00000004\" allowed_cpuset=\"0x00000004\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\"/>\n\
        </object>\n\
        <object type=\"Core\" os_index=\"3\"\
 cpuset=\"0x00000008\" complete_cpuset=\"0x00000008\"\
 online_cpuset=\"0x00000008\" allowed_cpuset=\"0x00000008\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\">\n\
          <object type=\"PU\" os_index=\"3\"\
 cpuset=\"0x00000008\" complete_cpuset=\"0x00000008\"\
 online_cpuset=\"0x00000008\" allowed_cpuset=\"0x00000008\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
  </object>\n\
</topology>\n";

/* No packages: 2 NUMA nodes with cores directly beneath, 4 GiB each */
static const char xml_no_pkg[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\"\
 cpuset=\"0x0000000f\" complete_cpuset=\"0x0000000f\"\
 online_cpuset=\"0x0000000f\" allowed_cpuset=\"0x0000000f\"\
 nodeset=\"0x00000003\" complete_nodeset=\"0x00000003\"\
 allowed_nodeset=\"0x00000003\">\n\
    <info name=\"HostName\" value=\"testhost\"/>\n\
    <object type=\"NUMANode\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\" local_memory=\"4294967296\">\n\
      <object type=\"Core\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"PU\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
      </object>\n\
      <object type=\"Core\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"PU\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
      </object>\n\
    </object>\n\
    <object type=\"NUMANode\" os_index=\"1\"\
 cpuset=\"0x0000000c\" complete_cpuset=\"0x0000000c\"\
 online_cpuset=\"0x0000000c\" allowed_cpuset=\"0x0000000c\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\" local_memory=\"4294967296\">\n\
      <object type=\"Core\" os_index=\"2\"\
 cpuset=\"0x00000004\" complete_cpuset=\"0x00000004\"\
 online_cpuset=\"0x00000004\" allowed_cpuset=\"0x00000004\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\">\n\
        <object type=\"PU\" os_index=\"2\"\
 cpuset=\"0x00000004\" complete_cpuset=\"0x00000004\"\
 online_cpuset=\"0x00000004\" allowed_cpuset=\"0x00000004\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\"/>\n\
      </object>\n\
      <object type=\"Core\" os_index=\"3\"\
 cpuset=\"0x0000000c\" complete_cpuset=\"0x0000000c\"\
 online_cpuset=\"0x0000000c\" allowed_cpuset=\"0x0000000c\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\">\n\
        <object type=\"PU\" os_index=\"3\"\
 cpuset=\"0x00000008\" complete_cpuset=\"0x00000008\"\
 online_cpuset=\"0x00000008\" allowed_cpuset=\"0x00000008\"\
 nodeset=\"0x00000002\" complete_nodeset=\"0x00000002\"\
 allowed_nodeset=\"0x00000002\"/>\n\
      </object>\n\
    </object>\n\
  </object>\n\
</topology>\n";

/* GPU under NUMANode: single package with 1 GPU */
static const char xml_nps1_gpu[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
    <info name=\"HostName\" value=\"testhost\"/>\n\
    <object type=\"NUMANode\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\" local_memory=\"8589934592\">\n\
      <object type=\"Package\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"Core\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
        <object type=\"Core\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
    <object type=\"Bridge\" os_index=\"0\" bridge_type=\"0-1\" depth=\"0\"\
 bridge_pci=\"0000:[00-01]\">\n\
      <object type=\"PCIDev\" os_index=\"4096\" name=\"Test GPU 0\"\
 pci_busid=\"0000:01:00.0\" pci_type=\"0302 [10de:1234] [10de:0000] a1\"\
 pci_link_speed=\"0.000000\">\n\
        <object type=\"OSDev\" name=\"cuda0\" osdev_type=\"5\">\n\
          <info name=\"CoProcType\" value=\"CUDA\"/>\n\
          <info name=\"Backend\" value=\"CUDA\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
  </object>\n\
</topology>\n";

/* Test basic TreePool topology generation */
void test_basic_topologies (void)
{
    hwloc_topology_t topo;
    json_t *result, *arr, *e;
    const char *s;

    /* NPS1 single package: single-element socket collapses to flat leaf */
    topo = rhwloc_xml_topology_load (xml_nps1_1pkg, RHWLOC_NO_RESTRICT);
    if (!topo)
        BAIL_OUT ("failed to load xml_nps1_1pkg topology");
    result = rhwloc_treepool_topo (topo, NULL);
    hwloc_topology_destroy (topo);
    ok (result != NULL,
        "NPS1 1-pkg returns non-NULL");
    ok (json_object_get (result, "socket") == NULL,
        "NPS1 1-pkg topo has no socket array (collapsed)");
    s = json_string_value (json_object_get (result, "cores"));
    ok (s && strcmp (s, "0-1") == 0,
        "NPS1 1-pkg topo.cores == \"0-1\"");
    ok (json_integer_value (json_object_get (result, "memory")) == 8,
        "NPS1 1-pkg topo.memory == 8");
    json_decref (result);

    /* NPS1 two packages: two socket entries, each folding its sole NUMA */
    topo = rhwloc_xml_topology_load (xml_nps1_2pkg, RHWLOC_NO_RESTRICT);
    if (!topo)
        BAIL_OUT ("failed to load xml_nps1_2pkg topology");
    result = rhwloc_treepool_topo (topo, NULL);
    hwloc_topology_destroy (topo);
    ok (result != NULL,
        "NPS1 2-pkg returns non-NULL");
    arr = json_object_get (result, "socket");
    ok (json_is_array (arr) && json_array_size (arr) == 2,
        "NPS1 2-pkg topo.socket has 2 entries");
    e = json_array_get (arr, 0);
    s = json_string_value (json_object_get (e, "cores"));
    ok (s && strcmp (s, "0-1") == 0,
        "NPS1 2-pkg topo.socket[0].cores == \"0-1\"");
    ok (json_integer_value (json_object_get (e, "memory")) == 8,
        "NPS1 2-pkg topo.socket[0].memory == 8");
    e = json_array_get (arr, 1);
    s = json_string_value (json_object_get (e, "cores"));
    ok (s && strcmp (s, "2-3") == 0,
        "NPS1 2-pkg topo.socket[1].cores == \"2-3\"");
    ok (json_integer_value (json_object_get (e, "memory")) == 8,
        "NPS1 2-pkg topo.socket[1].memory == 8");
    json_decref (result);

    /* No packages: two NUMA nodes directly in topo */
    topo = rhwloc_xml_topology_load (xml_no_pkg, RHWLOC_NO_RESTRICT);
    if (!topo)
        BAIL_OUT ("failed to load xml_no_pkg topology");
    result = rhwloc_treepool_topo (topo, NULL);
    hwloc_topology_destroy (topo);
    ok (result != NULL,
        "no-pkg returns non-NULL");
    arr = json_object_get (result, "numa");
    ok (json_is_array (arr) && json_array_size (arr) == 2,
        "no-pkg topo.numa has 2 entries");
    e = json_array_get (arr, 0);
    s = json_string_value (json_object_get (e, "cores"));
    ok (s && strcmp (s, "0-1") == 0,
        "no-pkg topo.numa[0].cores == \"0-1\"");
    ok (json_integer_value (json_object_get (e, "memory")) == 4,
        "no-pkg topo.numa[0].memory == 4");
    e = json_array_get (arr, 1);
    s = json_string_value (json_object_get (e, "cores"));
    ok (s && strcmp (s, "2-3") == 0,
        "no-pkg topo.numa[1].cores == \"2-3\"");
    ok (json_integer_value (json_object_get (e, "memory")) == 4,
        "no-pkg topo.numa[1].memory == 4");
    json_decref (result);

    /* GPU at Machine level (hwloc 2.x I/O placement): single socket collapses,
     * GPU assigned to NUMA via cpuset matching. Tests gpu_belongs_to_obj()
     * fallback path for I/O devices not in CPU object parent chain.
     */
    topo = rhwloc_xml_topology_load (xml_nps1_gpu, RHWLOC_NO_RESTRICT);
    if (!topo)
        BAIL_OUT ("failed to load xml_nps1_gpu topology");
    result = rhwloc_treepool_topo (topo, NULL);
    hwloc_topology_destroy (topo);
    ok (result != NULL,
        "GPU topology returns non-NULL");
    s = json_string_value (json_object_get (result, "gpus"));
    ok (s && strcmp (s, "0") == 0,
        "GPU topology topo.gpus == \"0\"");
    ok (json_integer_value (json_object_get (result, "memory")) == 8,
        "GPU topology topo.memory == 8");
    json_decref (result);
}

/* Topology with NUMA node with zero memory */
static const char xml_zero_mem_numa[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
    <info name=\"HostName\" value=\"testhost\"/>\n\
    <object type=\"NUMANode\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\" local_memory=\"0\">\n\
      <object type=\"Package\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"Core\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
        <object type=\"Core\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
          <object type=\"PU\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\"/>\n\
        </object>\n\
      </object>\n\
    </object>\n\
  </object>\n\
</topology>\n";

/* Topology with L3 cache between Package and Core */
static const char xml_with_cache[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
    <info name=\"HostName\" value=\"testhost\"/>\n\
    <object type=\"NUMANode\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\" local_memory=\"8589934592\">\n\
      <object type=\"Package\" os_index=\"0\"\
 cpuset=\"0x00000003\" complete_cpuset=\"0x00000003\"\
 online_cpuset=\"0x00000003\" allowed_cpuset=\"0x00000003\"\
 nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\"\
 allowed_nodeset=\"0x00000001\">\n\
        <object type=\"L3Cache\" cpuset=\"0x00000003\"\
 complete_cpuset=\"0x00000003\" online_cpuset=\"0x00000003\"\
 allowed_cpuset=\"0x00000003\" cache_size=\"16777216\"\
 depth=\"3\" cache_linesize=\"64\" cache_associativity=\"16\"\
 cache_type=\"0\">\n\
          <object type=\"Core\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\">\n\
            <object type=\"PU\" os_index=\"0\"\
 cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\"\
 online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\"/>\n\
          </object>\n\
          <object type=\"Core\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\">\n\
            <object type=\"PU\" os_index=\"1\"\
 cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\"\
 online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\"/>\n\
          </object>\n\
        </object>\n\
      </object>\n\
    </object>\n\
  </object>\n\
</topology>\n";


/* Test error paths and errno handling */
void test_error_paths (void)
{
    json_t *result;
    flux_error_t error;

    /* NULL topology */
    err_init (&error);
    errno = 0;
    result = rhwloc_treepool_topo (NULL, &error);
    ok (result == NULL,
        "NULL topology returns NULL");
    ok (errno == EINVAL,
        "NULL topology sets errno=EINVAL (got %d)", errno);
    ok (error.text[0] != '\0',
        "NULL topology sets error message");
    diag ("error: %s", error.text);

    /* NULL xml string */
    err_init (&error);
    errno = 0;
    char *json_str = rhwloc_treepool_topo_to_json (NULL, &error);
    ok (json_str == NULL,
        "NULL xml returns NULL");
    ok (errno == EINVAL,
        "NULL xml sets errno=EINVAL");
    ok (error.text[0] != '\0',
        "NULL xml sets error message");
    diag ("error: %s", error.text);

}

/* Test rhwloc_treepool_topo_to_json(), the entry point used by the Python
 * FFI bindings.  test_error_paths() only covers its NULL-argument guard;
 * exercise the success round-trip and the malformed-XML failure branch.
 */
void test_to_json (void)
{
    flux_error_t error;
    char *json_str;
    json_t *parsed;
    const char *s;

    /* Valid XML round-trips to a parseable JSON topology string */
    err_init (&error);
    errno = 0;
    json_str = rhwloc_treepool_topo_to_json (xml_nps1_1pkg, &error);
    ok (json_str != NULL,
        "valid xml returns non-NULL json string");
    if (!json_str)
        diag ("error: %s", error.text);
    parsed = json_str ? json_loads (json_str, 0, NULL) : NULL;
    ok (parsed != NULL,
        "returned json string parses");
    s = json_string_value (json_object_get (parsed, "cores"));
    ok (s && strcmp (s, "0-1") == 0,
        "round-trip json topo.cores == \"0-1\"");
    json_decref (parsed);
    free (json_str);

    /* Malformed XML fails with errno set and error message filled */
    err_init (&error);
    errno = 0;
    json_str = rhwloc_treepool_topo_to_json ("<not valid hwloc xml>",
                                             &error);
    ok (json_str == NULL,
        "malformed xml returns NULL");
    ok (errno != 0,
        "malformed xml sets errno (got %d)", errno);
    ok (error.text[0] != '\0',
        "malformed xml sets error message");
    diag ("error: %s", error.text);
}

/* Test advanced topology structures */
void test_advanced_topologies (void)
{
    hwloc_topology_t topo;
    json_t *result;
    const char *s;

    /* Topology with zero-memory NUMA node */
    topo = rhwloc_xml_topology_load (xml_zero_mem_numa, RHWLOC_NO_RESTRICT);
    if (!topo)
        BAIL_OUT ("failed to load xml_zero_mem_numa topology");
    result = rhwloc_treepool_topo (topo, NULL);
    hwloc_topology_destroy (topo);
    ok (result != NULL,
        "zero-memory NUMA topology returns non-NULL");
    s = json_string_value (json_object_get (result, "cores"));
    ok (s && strcmp (s, "0-1") == 0,
        "zero-memory NUMA topo.cores == \"0-1\"");
    ok (json_object_get (result, "memory") == NULL,
        "zero-memory NUMA has no memory field (0 GiB rounds down)");
    json_decref (result);

    /* Topology with L3 cache between Package and Core (boring intermediate) */
    topo = rhwloc_xml_topology_load (xml_with_cache, RHWLOC_NO_RESTRICT);
    if (!topo)
        BAIL_OUT ("failed to load xml_with_cache topology");
    result = rhwloc_treepool_topo (topo, NULL);
    hwloc_topology_destroy (topo);
    ok (result != NULL,
        "topology with L3 cache returns non-NULL");
    s = json_string_value (json_object_get (result, "cores"));
    ok (s && strcmp (s, "0-1") == 0,
        "L3 cache topology descends through cache to find cores");
    ok (json_integer_value (json_object_get (result, "memory")) == 8,
        "L3 cache topology memory == 8");
    json_decref (result);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_basic_topologies ();
    test_error_paths ();
    test_to_json ();
    test_advanced_topologies ();

    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
