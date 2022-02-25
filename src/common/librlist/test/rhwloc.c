/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/hostlist.h>

#include "src/common/libtap/tap.h"
#include "rlist.h"
#include "rhwloc.h"

const char xml1[] = "\
<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\
<!DOCTYPE topology SYSTEM \"hwloc.dtd\">\n\
<topology>\n\
  <object type=\"Machine\" os_index=\"0\" cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" complete_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" online_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" allowed_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" nodeset=\"0xf0000000,,,,,,,0x00000101\" complete_nodeset=\"0xf0000000,,,,,,,0x00000101\" allowed_nodeset=\"0xf0000000,,,,,,,0x00000101\">\n\
    <page_type size=\"65536\" count=\"0\"/>\n\
    <page_type size=\"2097152\" count=\"0\"/>\n\
    <page_type size=\"1073741824\" count=\"0\"/>\n\
    <info name=\"PlatformName\" value=\"PowerNV\"/>\n\
    <info name=\"PlatformModel\" value=\"PowerNV 8335-GTW\"/>\n\
    <info name=\"Backend\" value=\"Linux\"/>\n\
    <info name=\"LinuxCgroup\" value=\"/allocation_54106\"/>\n\
    <info name=\"OSName\" value=\"Linux\"/>\n\
    <info name=\"OSRelease\" value=\"4.14.0-49.6.1.el7a.ppc64le\"/>\n\
    <info name=\"OSVersion\" value=\"#1 SMP Wed May 16 21:05:05 UTC 2018\"/>\n\
    <info name=\"HostName\" value=\"sierra3179\"/>\n\
    <info name=\"Architecture\" value=\"ppc64le\"/>\n\
    <info name=\"hwlocVersion\" value=\"1.11.10\"/>\n\
    <info name=\"ProcessName\" value=\"hwloc_xml\"/>\n\
    <distances nbobjs=\"6\" relative_depth=\"2\" latency_base=\"10.000000\">\n\
      <latency value=\"1.000000\"/>\n\
      <latency value=\"4.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"4.000000\"/>\n\
      <latency value=\"1.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"1.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"1.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"1.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"8.000000\"/>\n\
      <latency value=\"1.000000\"/>\n\
    </distances>\n\
    <object type=\"Group\" cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" complete_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" online_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" allowed_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff,0xffffffff\" nodeset=\"0x00000101\" complete_nodeset=\"0x00000101\" allowed_nodeset=\"0x00000101\" depth=\"0\">\n\
      <object type=\"NUMANode\" os_index=\"0\" cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" complete_cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" online_cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" allowed_cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" local_memory=\"137132310528\">\n\
        <page_type size=\"65536\" count=\"2092473\"/>\n\
        <page_type size=\"2097152\" count=\"0\"/>\n\
        <page_type size=\"1073741824\" count=\"0\"/>\n\
        <object type=\"Package\" os_index=\"0\" cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" complete_cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" online_cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" allowed_cpuset=\"0x00ffffff,0xffffffff,0xffffffff\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
          <info name=\"CPUModel\" value=\"POWER9, altivec supported\"/>\n\
          <info name=\"CPURevision\" value=\"2.1 (pvr 004e 1201)\"/>\n\
          <object type=\"Cache\" cpuset=\"0x000000ff\" complete_cpuset=\"0x000000ff\" online_cpuset=\"0x000000ff\" allowed_cpuset=\"0x000000ff\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x000000ff\" complete_cpuset=\"0x000000ff\" online_cpuset=\"0x000000ff\" allowed_cpuset=\"0x000000ff\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0000000f\" complete_cpuset=\"0x0000000f\" online_cpuset=\"0x0000000f\" allowed_cpuset=\"0x0000000f\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"8\" cpuset=\"0x0000000f\" complete_cpuset=\"0x0000000f\" online_cpuset=\"0x0000000f\" allowed_cpuset=\"0x0000000f\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"0\" cpuset=\"0x00000001\" complete_cpuset=\"0x00000001\" online_cpuset=\"0x00000001\" allowed_cpuset=\"0x00000001\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"1\" cpuset=\"0x00000002\" complete_cpuset=\"0x00000002\" online_cpuset=\"0x00000002\" allowed_cpuset=\"0x00000002\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"2\" cpuset=\"0x00000004\" complete_cpuset=\"0x00000004\" online_cpuset=\"0x00000004\" allowed_cpuset=\"0x00000004\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"3\" cpuset=\"0x00000008\" complete_cpuset=\"0x00000008\" online_cpuset=\"0x00000008\" allowed_cpuset=\"0x00000008\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x000000f0\" complete_cpuset=\"0x000000f0\" online_cpuset=\"0x000000f0\" allowed_cpuset=\"0x000000f0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"12\" cpuset=\"0x000000f0\" complete_cpuset=\"0x000000f0\" online_cpuset=\"0x000000f0\" allowed_cpuset=\"0x000000f0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"4\" cpuset=\"0x00000010\" complete_cpuset=\"0x00000010\" online_cpuset=\"0x00000010\" allowed_cpuset=\"0x00000010\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"5\" cpuset=\"0x00000020\" complete_cpuset=\"0x00000020\" online_cpuset=\"0x00000020\" allowed_cpuset=\"0x00000020\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"6\" cpuset=\"0x00000040\" complete_cpuset=\"0x00000040\" online_cpuset=\"0x00000040\" allowed_cpuset=\"0x00000040\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"7\" cpuset=\"0x00000080\" complete_cpuset=\"0x00000080\" online_cpuset=\"0x00000080\" allowed_cpuset=\"0x00000080\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x0000ff00\" complete_cpuset=\"0x0000ff00\" online_cpuset=\"0x0000ff00\" allowed_cpuset=\"0x0000ff00\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x0000ff00\" complete_cpuset=\"0x0000ff00\" online_cpuset=\"0x0000ff00\" allowed_cpuset=\"0x0000ff00\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x00000f00\" complete_cpuset=\"0x00000f00\" online_cpuset=\"0x00000f00\" allowed_cpuset=\"0x00000f00\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"16\" cpuset=\"0x00000f00\" complete_cpuset=\"0x00000f00\" online_cpuset=\"0x00000f00\" allowed_cpuset=\"0x00000f00\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"8\" cpuset=\"0x00000100\" complete_cpuset=\"0x00000100\" online_cpuset=\"0x00000100\" allowed_cpuset=\"0x00000100\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"9\" cpuset=\"0x00000200\" complete_cpuset=\"0x00000200\" online_cpuset=\"0x00000200\" allowed_cpuset=\"0x00000200\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"10\" cpuset=\"0x00000400\" complete_cpuset=\"0x00000400\" online_cpuset=\"0x00000400\" allowed_cpuset=\"0x00000400\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"11\" cpuset=\"0x00000800\" complete_cpuset=\"0x00000800\" online_cpuset=\"0x00000800\" allowed_cpuset=\"0x00000800\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x0000f000\" complete_cpuset=\"0x0000f000\" online_cpuset=\"0x0000f000\" allowed_cpuset=\"0x0000f000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"20\" cpuset=\"0x0000f000\" complete_cpuset=\"0x0000f000\" online_cpuset=\"0x0000f000\" allowed_cpuset=\"0x0000f000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"12\" cpuset=\"0x00001000\" complete_cpuset=\"0x00001000\" online_cpuset=\"0x00001000\" allowed_cpuset=\"0x00001000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"13\" cpuset=\"0x00002000\" complete_cpuset=\"0x00002000\" online_cpuset=\"0x00002000\" allowed_cpuset=\"0x00002000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"14\" cpuset=\"0x00004000\" complete_cpuset=\"0x00004000\" online_cpuset=\"0x00004000\" allowed_cpuset=\"0x00004000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"15\" cpuset=\"0x00008000\" complete_cpuset=\"0x00008000\" online_cpuset=\"0x00008000\" allowed_cpuset=\"0x00008000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x00ff0000\" complete_cpuset=\"0x00ff0000\" online_cpuset=\"0x00ff0000\" allowed_cpuset=\"0x00ff0000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x00ff0000\" complete_cpuset=\"0x00ff0000\" online_cpuset=\"0x00ff0000\" allowed_cpuset=\"0x00ff0000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x000f0000\" complete_cpuset=\"0x000f0000\" online_cpuset=\"0x000f0000\" allowed_cpuset=\"0x000f0000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"24\" cpuset=\"0x000f0000\" complete_cpuset=\"0x000f0000\" online_cpuset=\"0x000f0000\" allowed_cpuset=\"0x000f0000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"16\" cpuset=\"0x00010000\" complete_cpuset=\"0x00010000\" online_cpuset=\"0x00010000\" allowed_cpuset=\"0x00010000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"17\" cpuset=\"0x00020000\" complete_cpuset=\"0x00020000\" online_cpuset=\"0x00020000\" allowed_cpuset=\"0x00020000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"18\" cpuset=\"0x00040000\" complete_cpuset=\"0x00040000\" online_cpuset=\"0x00040000\" allowed_cpuset=\"0x00040000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"19\" cpuset=\"0x00080000\" complete_cpuset=\"0x00080000\" online_cpuset=\"0x00080000\" allowed_cpuset=\"0x00080000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x00f00000\" complete_cpuset=\"0x00f00000\" online_cpuset=\"0x00f00000\" allowed_cpuset=\"0x00f00000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"28\" cpuset=\"0x00f00000\" complete_cpuset=\"0x00f00000\" online_cpuset=\"0x00f00000\" allowed_cpuset=\"0x00f00000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"20\" cpuset=\"0x00100000\" complete_cpuset=\"0x00100000\" online_cpuset=\"0x00100000\" allowed_cpuset=\"0x00100000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"21\" cpuset=\"0x00200000\" complete_cpuset=\"0x00200000\" online_cpuset=\"0x00200000\" allowed_cpuset=\"0x00200000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"22\" cpuset=\"0x00400000\" complete_cpuset=\"0x00400000\" online_cpuset=\"0x00400000\" allowed_cpuset=\"0x00400000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"23\" cpuset=\"0x00800000\" complete_cpuset=\"0x00800000\" online_cpuset=\"0x00800000\" allowed_cpuset=\"0x00800000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0xff000000\" complete_cpuset=\"0xff000000\" online_cpuset=\"0xff000000\" allowed_cpuset=\"0xff000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0xff000000\" complete_cpuset=\"0xff000000\" online_cpuset=\"0xff000000\" allowed_cpuset=\"0xff000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0f000000\" complete_cpuset=\"0x0f000000\" online_cpuset=\"0x0f000000\" allowed_cpuset=\"0x0f000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"32\" cpuset=\"0x0f000000\" complete_cpuset=\"0x0f000000\" online_cpuset=\"0x0f000000\" allowed_cpuset=\"0x0f000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"24\" cpuset=\"0x01000000\" complete_cpuset=\"0x01000000\" online_cpuset=\"0x01000000\" allowed_cpuset=\"0x01000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"25\" cpuset=\"0x02000000\" complete_cpuset=\"0x02000000\" online_cpuset=\"0x02000000\" allowed_cpuset=\"0x02000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"26\" cpuset=\"0x04000000\" complete_cpuset=\"0x04000000\" online_cpuset=\"0x04000000\" allowed_cpuset=\"0x04000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"27\" cpuset=\"0x08000000\" complete_cpuset=\"0x08000000\" online_cpuset=\"0x08000000\" allowed_cpuset=\"0x08000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0xf0000000\" complete_cpuset=\"0xf0000000\" online_cpuset=\"0xf0000000\" allowed_cpuset=\"0xf0000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"36\" cpuset=\"0xf0000000\" complete_cpuset=\"0xf0000000\" online_cpuset=\"0xf0000000\" allowed_cpuset=\"0xf0000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"28\" cpuset=\"0x10000000\" complete_cpuset=\"0x10000000\" online_cpuset=\"0x10000000\" allowed_cpuset=\"0x10000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"29\" cpuset=\"0x20000000\" complete_cpuset=\"0x20000000\" online_cpuset=\"0x20000000\" allowed_cpuset=\"0x20000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"30\" cpuset=\"0x40000000\" complete_cpuset=\"0x40000000\" online_cpuset=\"0x40000000\" allowed_cpuset=\"0x40000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"31\" cpuset=\"0x80000000\" complete_cpuset=\"0x80000000\" online_cpuset=\"0x80000000\" allowed_cpuset=\"0x80000000\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x000000ff,0x0\" complete_cpuset=\"0x000000ff,0x0\" online_cpuset=\"0x000000ff,0x0\" allowed_cpuset=\"0x000000ff,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x000000ff,0x0\" complete_cpuset=\"0x000000ff,0x0\" online_cpuset=\"0x000000ff,0x0\" allowed_cpuset=\"0x000000ff,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0000000f,0x0\" complete_cpuset=\"0x0000000f,0x0\" online_cpuset=\"0x0000000f,0x0\" allowed_cpuset=\"0x0000000f,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"40\" cpuset=\"0x0000000f,0x0\" complete_cpuset=\"0x0000000f,0x0\" online_cpuset=\"0x0000000f,0x0\" allowed_cpuset=\"0x0000000f,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"32\" cpuset=\"0x00000001,0x0\" complete_cpuset=\"0x00000001,0x0\" online_cpuset=\"0x00000001,0x0\" allowed_cpuset=\"0x00000001,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"33\" cpuset=\"0x00000002,0x0\" complete_cpuset=\"0x00000002,0x0\" online_cpuset=\"0x00000002,0x0\" allowed_cpuset=\"0x00000002,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"34\" cpuset=\"0x00000004,0x0\" complete_cpuset=\"0x00000004,0x0\" online_cpuset=\"0x00000004,0x0\" allowed_cpuset=\"0x00000004,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"35\" cpuset=\"0x00000008,0x0\" complete_cpuset=\"0x00000008,0x0\" online_cpuset=\"0x00000008,0x0\" allowed_cpuset=\"0x00000008,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x000000f0,0x0\" complete_cpuset=\"0x000000f0,0x0\" online_cpuset=\"0x000000f0,0x0\" allowed_cpuset=\"0x000000f0,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"44\" cpuset=\"0x000000f0,0x0\" complete_cpuset=\"0x000000f0,0x0\" online_cpuset=\"0x000000f0,0x0\" allowed_cpuset=\"0x000000f0,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"36\" cpuset=\"0x00000010,0x0\" complete_cpuset=\"0x00000010,0x0\" online_cpuset=\"0x00000010,0x0\" allowed_cpuset=\"0x00000010,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"37\" cpuset=\"0x00000020,0x0\" complete_cpuset=\"0x00000020,0x0\" online_cpuset=\"0x00000020,0x0\" allowed_cpuset=\"0x00000020,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"38\" cpuset=\"0x00000040,0x0\" complete_cpuset=\"0x00000040,0x0\" online_cpuset=\"0x00000040,0x0\" allowed_cpuset=\"0x00000040,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"39\" cpuset=\"0x00000080,0x0\" complete_cpuset=\"0x00000080,0x0\" online_cpuset=\"0x00000080,0x0\" allowed_cpuset=\"0x00000080,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x0000ff00,0x0\" complete_cpuset=\"0x0000ff00,0x0\" online_cpuset=\"0x0000ff00,0x0\" allowed_cpuset=\"0x0000ff00,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x0000ff00,0x0\" complete_cpuset=\"0x0000ff00,0x0\" online_cpuset=\"0x0000ff00,0x0\" allowed_cpuset=\"0x0000ff00,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x00000f00,0x0\" complete_cpuset=\"0x00000f00,0x0\" online_cpuset=\"0x00000f00,0x0\" allowed_cpuset=\"0x00000f00,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"48\" cpuset=\"0x00000f00,0x0\" complete_cpuset=\"0x00000f00,0x0\" online_cpuset=\"0x00000f00,0x0\" allowed_cpuset=\"0x00000f00,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"40\" cpuset=\"0x00000100,0x0\" complete_cpuset=\"0x00000100,0x0\" online_cpuset=\"0x00000100,0x0\" allowed_cpuset=\"0x00000100,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"41\" cpuset=\"0x00000200,0x0\" complete_cpuset=\"0x00000200,0x0\" online_cpuset=\"0x00000200,0x0\" allowed_cpuset=\"0x00000200,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"42\" cpuset=\"0x00000400,0x0\" complete_cpuset=\"0x00000400,0x0\" online_cpuset=\"0x00000400,0x0\" allowed_cpuset=\"0x00000400,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"43\" cpuset=\"0x00000800,0x0\" complete_cpuset=\"0x00000800,0x0\" online_cpuset=\"0x00000800,0x0\" allowed_cpuset=\"0x00000800,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x0000f000,0x0\" complete_cpuset=\"0x0000f000,0x0\" online_cpuset=\"0x0000f000,0x0\" allowed_cpuset=\"0x0000f000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"52\" cpuset=\"0x0000f000,0x0\" complete_cpuset=\"0x0000f000,0x0\" online_cpuset=\"0x0000f000,0x0\" allowed_cpuset=\"0x0000f000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"44\" cpuset=\"0x00001000,0x0\" complete_cpuset=\"0x00001000,0x0\" online_cpuset=\"0x00001000,0x0\" allowed_cpuset=\"0x00001000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"45\" cpuset=\"0x00002000,0x0\" complete_cpuset=\"0x00002000,0x0\" online_cpuset=\"0x00002000,0x0\" allowed_cpuset=\"0x00002000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"46\" cpuset=\"0x00004000,0x0\" complete_cpuset=\"0x00004000,0x0\" online_cpuset=\"0x00004000,0x0\" allowed_cpuset=\"0x00004000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"47\" cpuset=\"0x00008000,0x0\" complete_cpuset=\"0x00008000,0x0\" online_cpuset=\"0x00008000,0x0\" allowed_cpuset=\"0x00008000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x00ff0000,0x0\" complete_cpuset=\"0x00ff0000,0x0\" online_cpuset=\"0x00ff0000,0x0\" allowed_cpuset=\"0x00ff0000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x00ff0000,0x0\" complete_cpuset=\"0x00ff0000,0x0\" online_cpuset=\"0x00ff0000,0x0\" allowed_cpuset=\"0x00ff0000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x000f0000,0x0\" complete_cpuset=\"0x000f0000,0x0\" online_cpuset=\"0x000f0000,0x0\" allowed_cpuset=\"0x000f0000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"56\" cpuset=\"0x000f0000,0x0\" complete_cpuset=\"0x000f0000,0x0\" online_cpuset=\"0x000f0000,0x0\" allowed_cpuset=\"0x000f0000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"48\" cpuset=\"0x00010000,0x0\" complete_cpuset=\"0x00010000,0x0\" online_cpuset=\"0x00010000,0x0\" allowed_cpuset=\"0x00010000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"49\" cpuset=\"0x00020000,0x0\" complete_cpuset=\"0x00020000,0x0\" online_cpuset=\"0x00020000,0x0\" allowed_cpuset=\"0x00020000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"50\" cpuset=\"0x00040000,0x0\" complete_cpuset=\"0x00040000,0x0\" online_cpuset=\"0x00040000,0x0\" allowed_cpuset=\"0x00040000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"51\" cpuset=\"0x00080000,0x0\" complete_cpuset=\"0x00080000,0x0\" online_cpuset=\"0x00080000,0x0\" allowed_cpuset=\"0x00080000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x00f00000,0x0\" complete_cpuset=\"0x00f00000,0x0\" online_cpuset=\"0x00f00000,0x0\" allowed_cpuset=\"0x00f00000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"60\" cpuset=\"0x00f00000,0x0\" complete_cpuset=\"0x00f00000,0x0\" online_cpuset=\"0x00f00000,0x0\" allowed_cpuset=\"0x00f00000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"52\" cpuset=\"0x00100000,0x0\" complete_cpuset=\"0x00100000,0x0\" online_cpuset=\"0x00100000,0x0\" allowed_cpuset=\"0x00100000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"53\" cpuset=\"0x00200000,0x0\" complete_cpuset=\"0x00200000,0x0\" online_cpuset=\"0x00200000,0x0\" allowed_cpuset=\"0x00200000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"54\" cpuset=\"0x00400000,0x0\" complete_cpuset=\"0x00400000,0x0\" online_cpuset=\"0x00400000,0x0\" allowed_cpuset=\"0x00400000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"55\" cpuset=\"0x00800000,0x0\" complete_cpuset=\"0x00800000,0x0\" online_cpuset=\"0x00800000,0x0\" allowed_cpuset=\"0x00800000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0xff000000,0x0\" complete_cpuset=\"0xff000000,0x0\" online_cpuset=\"0xff000000,0x0\" allowed_cpuset=\"0xff000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0xff000000,0x0\" complete_cpuset=\"0xff000000,0x0\" online_cpuset=\"0xff000000,0x0\" allowed_cpuset=\"0xff000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0f000000,0x0\" complete_cpuset=\"0x0f000000,0x0\" online_cpuset=\"0x0f000000,0x0\" allowed_cpuset=\"0x0f000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"64\" cpuset=\"0x0f000000,0x0\" complete_cpuset=\"0x0f000000,0x0\" online_cpuset=\"0x0f000000,0x0\" allowed_cpuset=\"0x0f000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"56\" cpuset=\"0x01000000,0x0\" complete_cpuset=\"0x01000000,0x0\" online_cpuset=\"0x01000000,0x0\" allowed_cpuset=\"0x01000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"57\" cpuset=\"0x02000000,0x0\" complete_cpuset=\"0x02000000,0x0\" online_cpuset=\"0x02000000,0x0\" allowed_cpuset=\"0x02000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"58\" cpuset=\"0x04000000,0x0\" complete_cpuset=\"0x04000000,0x0\" online_cpuset=\"0x04000000,0x0\" allowed_cpuset=\"0x04000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"59\" cpuset=\"0x08000000,0x0\" complete_cpuset=\"0x08000000,0x0\" online_cpuset=\"0x08000000,0x0\" allowed_cpuset=\"0x08000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0xf0000000,0x0\" complete_cpuset=\"0xf0000000,0x0\" online_cpuset=\"0xf0000000,0x0\" allowed_cpuset=\"0xf0000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"68\" cpuset=\"0xf0000000,0x0\" complete_cpuset=\"0xf0000000,0x0\" online_cpuset=\"0xf0000000,0x0\" allowed_cpuset=\"0xf0000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"60\" cpuset=\"0x10000000,0x0\" complete_cpuset=\"0x10000000,0x0\" online_cpuset=\"0x10000000,0x0\" allowed_cpuset=\"0x10000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"61\" cpuset=\"0x20000000,0x0\" complete_cpuset=\"0x20000000,0x0\" online_cpuset=\"0x20000000,0x0\" allowed_cpuset=\"0x20000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"62\" cpuset=\"0x40000000,0x0\" complete_cpuset=\"0x40000000,0x0\" online_cpuset=\"0x40000000,0x0\" allowed_cpuset=\"0x40000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"63\" cpuset=\"0x80000000,0x0\" complete_cpuset=\"0x80000000,0x0\" online_cpuset=\"0x80000000,0x0\" allowed_cpuset=\"0x80000000,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x000000ff,,0x0\" complete_cpuset=\"0x000000ff,,0x0\" online_cpuset=\"0x000000ff,,0x0\" allowed_cpuset=\"0x000000ff,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x000000ff,,0x0\" complete_cpuset=\"0x000000ff,,0x0\" online_cpuset=\"0x000000ff,,0x0\" allowed_cpuset=\"0x000000ff,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0000000f,,0x0\" complete_cpuset=\"0x0000000f,,0x0\" online_cpuset=\"0x0000000f,,0x0\" allowed_cpuset=\"0x0000000f,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"72\" cpuset=\"0x0000000f,,0x0\" complete_cpuset=\"0x0000000f,,0x0\" online_cpuset=\"0x0000000f,,0x0\" allowed_cpuset=\"0x0000000f,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"64\" cpuset=\"0x00000001,,0x0\" complete_cpuset=\"0x00000001,,0x0\" online_cpuset=\"0x00000001,,0x0\" allowed_cpuset=\"0x00000001,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"65\" cpuset=\"0x00000002,,0x0\" complete_cpuset=\"0x00000002,,0x0\" online_cpuset=\"0x00000002,,0x0\" allowed_cpuset=\"0x00000002,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"66\" cpuset=\"0x00000004,,0x0\" complete_cpuset=\"0x00000004,,0x0\" online_cpuset=\"0x00000004,,0x0\" allowed_cpuset=\"0x00000004,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"67\" cpuset=\"0x00000008,,0x0\" complete_cpuset=\"0x00000008,,0x0\" online_cpuset=\"0x00000008,,0x0\" allowed_cpuset=\"0x00000008,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x000000f0,,0x0\" complete_cpuset=\"0x000000f0,,0x0\" online_cpuset=\"0x000000f0,,0x0\" allowed_cpuset=\"0x000000f0,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"76\" cpuset=\"0x000000f0,,0x0\" complete_cpuset=\"0x000000f0,,0x0\" online_cpuset=\"0x000000f0,,0x0\" allowed_cpuset=\"0x000000f0,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"68\" cpuset=\"0x00000010,,0x0\" complete_cpuset=\"0x00000010,,0x0\" online_cpuset=\"0x00000010,,0x0\" allowed_cpuset=\"0x00000010,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"69\" cpuset=\"0x00000020,,0x0\" complete_cpuset=\"0x00000020,,0x0\" online_cpuset=\"0x00000020,,0x0\" allowed_cpuset=\"0x00000020,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"70\" cpuset=\"0x00000040,,0x0\" complete_cpuset=\"0x00000040,,0x0\" online_cpuset=\"0x00000040,,0x0\" allowed_cpuset=\"0x00000040,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"71\" cpuset=\"0x00000080,,0x0\" complete_cpuset=\"0x00000080,,0x0\" online_cpuset=\"0x00000080,,0x0\" allowed_cpuset=\"0x00000080,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x0000ff00,,0x0\" complete_cpuset=\"0x0000ff00,,0x0\" online_cpuset=\"0x0000ff00,,0x0\" allowed_cpuset=\"0x0000ff00,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x0000ff00,,0x0\" complete_cpuset=\"0x0000ff00,,0x0\" online_cpuset=\"0x0000ff00,,0x0\" allowed_cpuset=\"0x0000ff00,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x00000f00,,0x0\" complete_cpuset=\"0x00000f00,,0x0\" online_cpuset=\"0x00000f00,,0x0\" allowed_cpuset=\"0x00000f00,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"80\" cpuset=\"0x00000f00,,0x0\" complete_cpuset=\"0x00000f00,,0x0\" online_cpuset=\"0x00000f00,,0x0\" allowed_cpuset=\"0x00000f00,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"72\" cpuset=\"0x00000100,,0x0\" complete_cpuset=\"0x00000100,,0x0\" online_cpuset=\"0x00000100,,0x0\" allowed_cpuset=\"0x00000100,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"73\" cpuset=\"0x00000200,,0x0\" complete_cpuset=\"0x00000200,,0x0\" online_cpuset=\"0x00000200,,0x0\" allowed_cpuset=\"0x00000200,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"74\" cpuset=\"0x00000400,,0x0\" complete_cpuset=\"0x00000400,,0x0\" online_cpuset=\"0x00000400,,0x0\" allowed_cpuset=\"0x00000400,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"75\" cpuset=\"0x00000800,,0x0\" complete_cpuset=\"0x00000800,,0x0\" online_cpuset=\"0x00000800,,0x0\" allowed_cpuset=\"0x00000800,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x0000f000,,0x0\" complete_cpuset=\"0x0000f000,,0x0\" online_cpuset=\"0x0000f000,,0x0\" allowed_cpuset=\"0x0000f000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"84\" cpuset=\"0x0000f000,,0x0\" complete_cpuset=\"0x0000f000,,0x0\" online_cpuset=\"0x0000f000,,0x0\" allowed_cpuset=\"0x0000f000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"76\" cpuset=\"0x00001000,,0x0\" complete_cpuset=\"0x00001000,,0x0\" online_cpuset=\"0x00001000,,0x0\" allowed_cpuset=\"0x00001000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"77\" cpuset=\"0x00002000,,0x0\" complete_cpuset=\"0x00002000,,0x0\" online_cpuset=\"0x00002000,,0x0\" allowed_cpuset=\"0x00002000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"78\" cpuset=\"0x00004000,,0x0\" complete_cpuset=\"0x00004000,,0x0\" online_cpuset=\"0x00004000,,0x0\" allowed_cpuset=\"0x00004000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"79\" cpuset=\"0x00008000,,0x0\" complete_cpuset=\"0x00008000,,0x0\" online_cpuset=\"0x00008000,,0x0\" allowed_cpuset=\"0x00008000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x00ff0000,,0x0\" complete_cpuset=\"0x00ff0000,,0x0\" online_cpuset=\"0x00ff0000,,0x0\" allowed_cpuset=\"0x00ff0000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x00ff0000,,0x0\" complete_cpuset=\"0x00ff0000,,0x0\" online_cpuset=\"0x00ff0000,,0x0\" allowed_cpuset=\"0x00ff0000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x000f0000,,0x0\" complete_cpuset=\"0x000f0000,,0x0\" online_cpuset=\"0x000f0000,,0x0\" allowed_cpuset=\"0x000f0000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"88\" cpuset=\"0x000f0000,,0x0\" complete_cpuset=\"0x000f0000,,0x0\" online_cpuset=\"0x000f0000,,0x0\" allowed_cpuset=\"0x000f0000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"80\" cpuset=\"0x00010000,,0x0\" complete_cpuset=\"0x00010000,,0x0\" online_cpuset=\"0x00010000,,0x0\" allowed_cpuset=\"0x00010000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"81\" cpuset=\"0x00020000,,0x0\" complete_cpuset=\"0x00020000,,0x0\" online_cpuset=\"0x00020000,,0x0\" allowed_cpuset=\"0x00020000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"82\" cpuset=\"0x00040000,,0x0\" complete_cpuset=\"0x00040000,,0x0\" online_cpuset=\"0x00040000,,0x0\" allowed_cpuset=\"0x00040000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"83\" cpuset=\"0x00080000,,0x0\" complete_cpuset=\"0x00080000,,0x0\" online_cpuset=\"0x00080000,,0x0\" allowed_cpuset=\"0x00080000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x00f00000,,0x0\" complete_cpuset=\"0x00f00000,,0x0\" online_cpuset=\"0x00f00000,,0x0\" allowed_cpuset=\"0x00f00000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"92\" cpuset=\"0x00f00000,,0x0\" complete_cpuset=\"0x00f00000,,0x0\" online_cpuset=\"0x00f00000,,0x0\" allowed_cpuset=\"0x00f00000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\">\n\
                  <object type=\"PU\" os_index=\"84\" cpuset=\"0x00100000,,0x0\" complete_cpuset=\"0x00100000,,0x0\" online_cpuset=\"0x00100000,,0x0\" allowed_cpuset=\"0x00100000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"85\" cpuset=\"0x00200000,,0x0\" complete_cpuset=\"0x00200000,,0x0\" online_cpuset=\"0x00200000,,0x0\" allowed_cpuset=\"0x00200000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"86\" cpuset=\"0x00400000,,0x0\" complete_cpuset=\"0x00400000,,0x0\" online_cpuset=\"0x00400000,,0x0\" allowed_cpuset=\"0x00400000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                  <object type=\"PU\" os_index=\"87\" cpuset=\"0x00800000,,0x0\" complete_cpuset=\"0x00800000,,0x0\" online_cpuset=\"0x00800000,,0x0\" allowed_cpuset=\"0x00800000,,0x0\" nodeset=\"0x00000001\" complete_nodeset=\"0x00000001\" allowed_nodeset=\"0x00000001\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
        </object>\n\
        <object type=\"Bridge\" os_index=\"0\" bridge_type=\"0-1\" depth=\"0\" bridge_pci=\"0000:[00-01]\">\n\
          <object type=\"PCIDev\" os_index=\"4096\" name=\"Samsung Electronics Co Ltd NVMe SSD Controller 172Xa\" pci_busid=\"0000:01:00.0\" pci_type=\"0108 [144d:a822] [1014:0621] 01\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Samsung Electronics Co Ltd\"/>\n\
            <info name=\"PCIDevice\" value=\"NVMe SSD Controller 172Xa\"/>\n\
            <object type=\"OSDev\" name=\"nvme0n1\" osdev_type=\"0\">\n\
              <info name=\"LinuxDeviceID\" value=\"259:0\"/>\n\
              <info name=\"SerialNumber\" value=\"S3RVNA0JA01484\"/>\n\
              <info name=\"Type\" value=\"Disk\"/>\n\
            </object>\n\
          </object>\n\
        </object>\n\
        <object type=\"Bridge\" os_index=\"2\" bridge_type=\"0-1\" depth=\"0\" bridge_pci=\"0002:[00-02]\">\n\
          <object type=\"PCIDev\" os_index=\"2105344\" name=\"ASPEED Technology, Inc. ASPEED Graphics Family\" pci_busid=\"0002:02:00.0\" pci_type=\"0300 [1a03:2000] [1a03:2000] 41\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"ASPEED Technology, Inc.\"/>\n\
            <info name=\"PCIDevice\" value=\"ASPEED Graphics Family\"/>\n\
            <object type=\"OSDev\" name=\"card4\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"controlD68\" osdev_type=\"1\"/>\n\
          </object>\n\
        </object>\n\
        <object type=\"Bridge\" os_index=\"3\" bridge_type=\"0-1\" depth=\"0\" bridge_pci=\"0003:[00-01]\">\n\
          <object type=\"PCIDev\" os_index=\"3149824\" name=\"Mellanox Technologies MT28800 Family [ConnectX-5 Ex]\" pci_busid=\"0003:01:00.0\" pci_type=\"0207 [15b3:1019] [1014:0617] 00\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Mellanox Technologies\"/>\n\
            <info name=\"PCIDevice\" value=\"MT28800 Family [ConnectX-5 Ex]\"/>\n\
            <object type=\"OSDev\" name=\"hsi0\" osdev_type=\"2\">\n\
              <info name=\"Address\" value=\"00:00:10:87:fe:80:00:00:00:00:00:00:ec:0d:9a:03:00:ca:a4:18\"/>\n\
              <info name=\"Port\" value=\"1\"/>\n\
            </object>\n\
            <object type=\"OSDev\" name=\"mlx5_0\" osdev_type=\"3\">\n\
              <info name=\"NodeGUID\" value=\"ec0d:9a03:00ca:a418\"/>\n\
              <info name=\"SysImageGUID\" value=\"ec0d:9a03:00ca:a418\"/>\n\
              <info name=\"Port1State\" value=\"4\"/>\n\
              <info name=\"Port1LID\" value=\"0x32ed\"/>\n\
              <info name=\"Port1LMC\" value=\"0\"/>\n\
              <info name=\"Port1GID0\" value=\"fe80:0000:0000:0000:ec0d:9a03:00ca:a418\"/>\n\
            </object>\n\
          </object>\n\
          <object type=\"PCIDev\" os_index=\"3149825\" name=\"Mellanox Technologies MT28800 Family [ConnectX-5 Ex]\" pci_busid=\"0003:01:00.1\" pci_type=\"0207 [15b3:1019] [1014:0617] 00\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Mellanox Technologies\"/>\n\
            <info name=\"PCIDevice\" value=\"MT28800 Family [ConnectX-5 Ex]\"/>\n\
            <object type=\"OSDev\" name=\"hsi1\" osdev_type=\"2\">\n\
              <info name=\"Address\" value=\"00:00:18:87:fe:80:00:00:00:00:00:00:ec:0d:9a:03:00:ca:a4:19\"/>\n\
              <info name=\"Port\" value=\"1\"/>\n\
            </object>\n\
            <object type=\"OSDev\" name=\"mlx5_1\" osdev_type=\"3\">\n\
              <info name=\"NodeGUID\" value=\"ec0d:9a03:00ca:a419\"/>\n\
              <info name=\"SysImageGUID\" value=\"ec0d:9a03:00ca:a418\"/>\n\
              <info name=\"Port1State\" value=\"4\"/>\n\
              <info name=\"Port1LID\" value=\"0x32ee\"/>\n\
              <info name=\"Port1LMC\" value=\"0\"/>\n\
              <info name=\"Port1GID0\" value=\"fe80:0000:0000:0000:ec0d:9a03:00ca:a419\"/>\n\
            </object>\n\
          </object>\n\
        </object>\n\
        <object type=\"Bridge\" os_index=\"4\" bridge_type=\"0-1\" depth=\"0\" bridge_pci=\"0004:[00-0a]\">\n\
          <object type=\"PCIDev\" os_index=\"4206592\" name=\"Marvell Technology Group Ltd. 88SE9235 PCIe 2.0 x2 4-port SATA 6 Gb/s Controller\" pci_busid=\"0004:03:00.0\" pci_type=\"0106 [1b4b:9235] [1014:0612] 11\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Marvell Technology Group Ltd.\"/>\n\
            <info name=\"PCIDevice\" value=\"88SE9235 PCIe 2.0 x2 4-port SATA 6 Gb/s Controller\"/>\n\
          </object>\n\
          <object type=\"PCIDev\" os_index=\"4210688\" name=\"NVIDIA Corporation GV100GL [Tesla V100 SXM2]\" pci_busid=\"0004:04:00.0\" pci_type=\"0302 [10de:1db1] [10de:1212] a1\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"NVIDIA Corporation\"/>\n\
            <info name=\"PCIDevice\" value=\"GV100GL [Tesla V100 SXM2]\"/>\n\
            <object type=\"OSDev\" name=\"renderD128\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"card0\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"cuda0\" osdev_type=\"5\">\n\
              <info name=\"CoProcType\" value=\"CUDA\"/>\n\
              <info name=\"Backend\" value=\"CUDA\"/>\n\
              <info name=\"GPUVendor\" value=\"NVIDIA Corporation\"/>\n\
              <info name=\"GPUModel\" value=\"Tesla V100-SXM2-16GB\"/>\n\
              <info name=\"CUDAGlobalMemorySize\" value=\"16515072\"/>\n\
              <info name=\"CUDAL2CacheSize\" value=\"6144\"/>\n\
              <info name=\"CUDAMultiProcessors\" value=\"80\"/>\n\
              <info name=\"CUDACoresPerMP\" value=\"64\"/>\n\
              <info name=\"CUDASharedMemorySizePerMP\" value=\"48\"/>\n\
            </object>\n\
          </object>\n\
          <object type=\"PCIDev\" os_index=\"4214784\" name=\"NVIDIA Corporation GV100GL [Tesla V100 SXM2]\" pci_busid=\"0004:05:00.0\" pci_type=\"0302 [10de:1db1] [10de:1212] a1\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"NVIDIA Corporation\"/>\n\
            <info name=\"PCIDevice\" value=\"GV100GL [Tesla V100 SXM2]\"/>\n\
            <object type=\"OSDev\" name=\"card1\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"renderD129\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"cuda1\" osdev_type=\"5\">\n\
              <info name=\"CoProcType\" value=\"CUDA\"/>\n\
              <info name=\"Backend\" value=\"CUDA\"/>\n\
              <info name=\"GPUVendor\" value=\"NVIDIA Corporation\"/>\n\
              <info name=\"GPUModel\" value=\"Tesla V100-SXM2-16GB\"/>\n\
              <info name=\"CUDAGlobalMemorySize\" value=\"16515072\"/>\n\
              <info name=\"CUDAL2CacheSize\" value=\"6144\"/>\n\
              <info name=\"CUDAMultiProcessors\" value=\"80\"/>\n\
              <info name=\"CUDACoresPerMP\" value=\"64\"/>\n\
              <info name=\"CUDASharedMemorySizePerMP\" value=\"48\"/>\n\
            </object>\n\
          </object>\n\
        </object>\n\
        <object type=\"Bridge\" os_index=\"5\" bridge_type=\"0-1\" depth=\"0\" bridge_pci=\"0005:[00-01]\">\n\
          <object type=\"PCIDev\" os_index=\"5246976\" name=\"Broadcom Limited NetXtreme BCM5719 Gigabit Ethernet PCIe\" pci_busid=\"0005:01:00.0\" pci_type=\"0200 [14e4:1657] [14e4:1981] 01\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Broadcom Limited\"/>\n\
            <info name=\"PCIDevice\" value=\"NetXtreme BCM5719 Gigabit Ethernet PCIe\"/>\n\
            <object type=\"OSDev\" name=\"enP5p1s0f0\" osdev_type=\"2\">\n\
              <info name=\"Address\" value=\"70:e2:84:14:9d:1f\"/>\n\
            </object>\n\
          </object>\n\
          <object type=\"PCIDev\" os_index=\"5246977\" name=\"Broadcom Limited NetXtreme BCM5719 Gigabit Ethernet PCIe\" pci_busid=\"0005:01:00.1\" pci_type=\"0200 [14e4:1657] [14e4:1657] 01\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Broadcom Limited\"/>\n\
            <info name=\"PCIDevice\" value=\"NetXtreme BCM5719 Gigabit Ethernet PCIe\"/>\n\
            <object type=\"OSDev\" name=\"enP5p1s0f1\" osdev_type=\"2\">\n\
              <info name=\"Address\" value=\"70:e2:84:14:9d:20\"/>\n\
            </object>\n\
          </object>\n\
        </object>\n\
      </object>\n\
      <object type=\"NUMANode\" os_index=\"8\" cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" complete_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" online_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" allowed_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" local_memory=\"137166979072\">\n\
        <page_type size=\"65536\" count=\"2093002\"/>\n\
        <page_type size=\"2097152\" count=\"0\"/>\n\
        <page_type size=\"1073741824\" count=\"0\"/>\n\
        <object type=\"Package\" os_index=\"8\" cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" complete_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" online_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" allowed_cpuset=\"0x0000ffff,0xffffffff,0xffffffff,0xff000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
          <info name=\"CPUModel\" value=\"POWER9, altivec supported\"/>\n\
          <info name=\"CPURevision\" value=\"2.1 (pvr 004e 1201)\"/>\n\
          <object type=\"Cache\" cpuset=\"0xff000000,,0x0\" complete_cpuset=\"0xff000000,,0x0\" online_cpuset=\"0xff000000,,0x0\" allowed_cpuset=\"0xff000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0xff000000,,0x0\" complete_cpuset=\"0xff000000,,0x0\" online_cpuset=\"0xff000000,,0x0\" allowed_cpuset=\"0xff000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0f000000,,0x0\" complete_cpuset=\"0x0f000000,,0x0\" online_cpuset=\"0x0f000000,,0x0\" allowed_cpuset=\"0x0f000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2048\" cpuset=\"0x0f000000,,0x0\" complete_cpuset=\"0x0f000000,,0x0\" online_cpuset=\"0x0f000000,,0x0\" allowed_cpuset=\"0x0f000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"88\" cpuset=\"0x01000000,,0x0\" complete_cpuset=\"0x01000000,,0x0\" online_cpuset=\"0x01000000,,0x0\" allowed_cpuset=\"0x01000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"89\" cpuset=\"0x02000000,,0x0\" complete_cpuset=\"0x02000000,,0x0\" online_cpuset=\"0x02000000,,0x0\" allowed_cpuset=\"0x02000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"90\" cpuset=\"0x04000000,,0x0\" complete_cpuset=\"0x04000000,,0x0\" online_cpuset=\"0x04000000,,0x0\" allowed_cpuset=\"0x04000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"91\" cpuset=\"0x08000000,,0x0\" complete_cpuset=\"0x08000000,,0x0\" online_cpuset=\"0x08000000,,0x0\" allowed_cpuset=\"0x08000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0xf0000000,,0x0\" complete_cpuset=\"0xf0000000,,0x0\" online_cpuset=\"0xf0000000,,0x0\" allowed_cpuset=\"0xf0000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2052\" cpuset=\"0xf0000000,,0x0\" complete_cpuset=\"0xf0000000,,0x0\" online_cpuset=\"0xf0000000,,0x0\" allowed_cpuset=\"0xf0000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"92\" cpuset=\"0x10000000,,0x0\" complete_cpuset=\"0x10000000,,0x0\" online_cpuset=\"0x10000000,,0x0\" allowed_cpuset=\"0x10000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"93\" cpuset=\"0x20000000,,0x0\" complete_cpuset=\"0x20000000,,0x0\" online_cpuset=\"0x20000000,,0x0\" allowed_cpuset=\"0x20000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"94\" cpuset=\"0x40000000,,0x0\" complete_cpuset=\"0x40000000,,0x0\" online_cpuset=\"0x40000000,,0x0\" allowed_cpuset=\"0x40000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"95\" cpuset=\"0x80000000,,0x0\" complete_cpuset=\"0x80000000,,0x0\" online_cpuset=\"0x80000000,,0x0\" allowed_cpuset=\"0x80000000,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x000000ff,,,0x0\" complete_cpuset=\"0x000000ff,,,0x0\" online_cpuset=\"0x000000ff,,,0x0\" allowed_cpuset=\"0x000000ff,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x000000ff,,,0x0\" complete_cpuset=\"0x000000ff,,,0x0\" online_cpuset=\"0x000000ff,,,0x0\" allowed_cpuset=\"0x000000ff,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0000000f,,,0x0\" complete_cpuset=\"0x0000000f,,,0x0\" online_cpuset=\"0x0000000f,,,0x0\" allowed_cpuset=\"0x0000000f,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2056\" cpuset=\"0x0000000f,,,0x0\" complete_cpuset=\"0x0000000f,,,0x0\" online_cpuset=\"0x0000000f,,,0x0\" allowed_cpuset=\"0x0000000f,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"96\" cpuset=\"0x00000001,,,0x0\" complete_cpuset=\"0x00000001,,,0x0\" online_cpuset=\"0x00000001,,,0x0\" allowed_cpuset=\"0x00000001,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"97\" cpuset=\"0x00000002,,,0x0\" complete_cpuset=\"0x00000002,,,0x0\" online_cpuset=\"0x00000002,,,0x0\" allowed_cpuset=\"0x00000002,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"98\" cpuset=\"0x00000004,,,0x0\" complete_cpuset=\"0x00000004,,,0x0\" online_cpuset=\"0x00000004,,,0x0\" allowed_cpuset=\"0x00000004,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"99\" cpuset=\"0x00000008,,,0x0\" complete_cpuset=\"0x00000008,,,0x0\" online_cpuset=\"0x00000008,,,0x0\" allowed_cpuset=\"0x00000008,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x000000f0,,,0x0\" complete_cpuset=\"0x000000f0,,,0x0\" online_cpuset=\"0x000000f0,,,0x0\" allowed_cpuset=\"0x000000f0,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2060\" cpuset=\"0x000000f0,,,0x0\" complete_cpuset=\"0x000000f0,,,0x0\" online_cpuset=\"0x000000f0,,,0x0\" allowed_cpuset=\"0x000000f0,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"100\" cpuset=\"0x00000010,,,0x0\" complete_cpuset=\"0x00000010,,,0x0\" online_cpuset=\"0x00000010,,,0x0\" allowed_cpuset=\"0x00000010,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"101\" cpuset=\"0x00000020,,,0x0\" complete_cpuset=\"0x00000020,,,0x0\" online_cpuset=\"0x00000020,,,0x0\" allowed_cpuset=\"0x00000020,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"102\" cpuset=\"0x00000040,,,0x0\" complete_cpuset=\"0x00000040,,,0x0\" online_cpuset=\"0x00000040,,,0x0\" allowed_cpuset=\"0x00000040,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"103\" cpuset=\"0x00000080,,,0x0\" complete_cpuset=\"0x00000080,,,0x0\" online_cpuset=\"0x00000080,,,0x0\" allowed_cpuset=\"0x00000080,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x0000ff00,,,0x0\" complete_cpuset=\"0x0000ff00,,,0x0\" online_cpuset=\"0x0000ff00,,,0x0\" allowed_cpuset=\"0x0000ff00,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x0000ff00,,,0x0\" complete_cpuset=\"0x0000ff00,,,0x0\" online_cpuset=\"0x0000ff00,,,0x0\" allowed_cpuset=\"0x0000ff00,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x00000f00,,,0x0\" complete_cpuset=\"0x00000f00,,,0x0\" online_cpuset=\"0x00000f00,,,0x0\" allowed_cpuset=\"0x00000f00,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2064\" cpuset=\"0x00000f00,,,0x0\" complete_cpuset=\"0x00000f00,,,0x0\" online_cpuset=\"0x00000f00,,,0x0\" allowed_cpuset=\"0x00000f00,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"104\" cpuset=\"0x00000100,,,0x0\" complete_cpuset=\"0x00000100,,,0x0\" online_cpuset=\"0x00000100,,,0x0\" allowed_cpuset=\"0x00000100,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"105\" cpuset=\"0x00000200,,,0x0\" complete_cpuset=\"0x00000200,,,0x0\" online_cpuset=\"0x00000200,,,0x0\" allowed_cpuset=\"0x00000200,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"106\" cpuset=\"0x00000400,,,0x0\" complete_cpuset=\"0x00000400,,,0x0\" online_cpuset=\"0x00000400,,,0x0\" allowed_cpuset=\"0x00000400,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"107\" cpuset=\"0x00000800,,,0x0\" complete_cpuset=\"0x00000800,,,0x0\" online_cpuset=\"0x00000800,,,0x0\" allowed_cpuset=\"0x00000800,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x0000f000,,,0x0\" complete_cpuset=\"0x0000f000,,,0x0\" online_cpuset=\"0x0000f000,,,0x0\" allowed_cpuset=\"0x0000f000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2068\" cpuset=\"0x0000f000,,,0x0\" complete_cpuset=\"0x0000f000,,,0x0\" online_cpuset=\"0x0000f000,,,0x0\" allowed_cpuset=\"0x0000f000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"108\" cpuset=\"0x00001000,,,0x0\" complete_cpuset=\"0x00001000,,,0x0\" online_cpuset=\"0x00001000,,,0x0\" allowed_cpuset=\"0x00001000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"109\" cpuset=\"0x00002000,,,0x0\" complete_cpuset=\"0x00002000,,,0x0\" online_cpuset=\"0x00002000,,,0x0\" allowed_cpuset=\"0x00002000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"110\" cpuset=\"0x00004000,,,0x0\" complete_cpuset=\"0x00004000,,,0x0\" online_cpuset=\"0x00004000,,,0x0\" allowed_cpuset=\"0x00004000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"111\" cpuset=\"0x00008000,,,0x0\" complete_cpuset=\"0x00008000,,,0x0\" online_cpuset=\"0x00008000,,,0x0\" allowed_cpuset=\"0x00008000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x00ff0000,,,0x0\" complete_cpuset=\"0x00ff0000,,,0x0\" online_cpuset=\"0x00ff0000,,,0x0\" allowed_cpuset=\"0x00ff0000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x00ff0000,,,0x0\" complete_cpuset=\"0x00ff0000,,,0x0\" online_cpuset=\"0x00ff0000,,,0x0\" allowed_cpuset=\"0x00ff0000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x000f0000,,,0x0\" complete_cpuset=\"0x000f0000,,,0x0\" online_cpuset=\"0x000f0000,,,0x0\" allowed_cpuset=\"0x000f0000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2072\" cpuset=\"0x000f0000,,,0x0\" complete_cpuset=\"0x000f0000,,,0x0\" online_cpuset=\"0x000f0000,,,0x0\" allowed_cpuset=\"0x000f0000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"112\" cpuset=\"0x00010000,,,0x0\" complete_cpuset=\"0x00010000,,,0x0\" online_cpuset=\"0x00010000,,,0x0\" allowed_cpuset=\"0x00010000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"113\" cpuset=\"0x00020000,,,0x0\" complete_cpuset=\"0x00020000,,,0x0\" online_cpuset=\"0x00020000,,,0x0\" allowed_cpuset=\"0x00020000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"114\" cpuset=\"0x00040000,,,0x0\" complete_cpuset=\"0x00040000,,,0x0\" online_cpuset=\"0x00040000,,,0x0\" allowed_cpuset=\"0x00040000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"115\" cpuset=\"0x00080000,,,0x0\" complete_cpuset=\"0x00080000,,,0x0\" online_cpuset=\"0x00080000,,,0x0\" allowed_cpuset=\"0x00080000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x00f00000,,,0x0\" complete_cpuset=\"0x00f00000,,,0x0\" online_cpuset=\"0x00f00000,,,0x0\" allowed_cpuset=\"0x00f00000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2076\" cpuset=\"0x00f00000,,,0x0\" complete_cpuset=\"0x00f00000,,,0x0\" online_cpuset=\"0x00f00000,,,0x0\" allowed_cpuset=\"0x00f00000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"116\" cpuset=\"0x00100000,,,0x0\" complete_cpuset=\"0x00100000,,,0x0\" online_cpuset=\"0x00100000,,,0x0\" allowed_cpuset=\"0x00100000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"117\" cpuset=\"0x00200000,,,0x0\" complete_cpuset=\"0x00200000,,,0x0\" online_cpuset=\"0x00200000,,,0x0\" allowed_cpuset=\"0x00200000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"118\" cpuset=\"0x00400000,,,0x0\" complete_cpuset=\"0x00400000,,,0x0\" online_cpuset=\"0x00400000,,,0x0\" allowed_cpuset=\"0x00400000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"119\" cpuset=\"0x00800000,,,0x0\" complete_cpuset=\"0x00800000,,,0x0\" online_cpuset=\"0x00800000,,,0x0\" allowed_cpuset=\"0x00800000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0xff000000,,,0x0\" complete_cpuset=\"0xff000000,,,0x0\" online_cpuset=\"0xff000000,,,0x0\" allowed_cpuset=\"0xff000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0xff000000,,,0x0\" complete_cpuset=\"0xff000000,,,0x0\" online_cpuset=\"0xff000000,,,0x0\" allowed_cpuset=\"0xff000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0f000000,,,0x0\" complete_cpuset=\"0x0f000000,,,0x0\" online_cpuset=\"0x0f000000,,,0x0\" allowed_cpuset=\"0x0f000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2080\" cpuset=\"0x0f000000,,,0x0\" complete_cpuset=\"0x0f000000,,,0x0\" online_cpuset=\"0x0f000000,,,0x0\" allowed_cpuset=\"0x0f000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"120\" cpuset=\"0x01000000,,,0x0\" complete_cpuset=\"0x01000000,,,0x0\" online_cpuset=\"0x01000000,,,0x0\" allowed_cpuset=\"0x01000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"121\" cpuset=\"0x02000000,,,0x0\" complete_cpuset=\"0x02000000,,,0x0\" online_cpuset=\"0x02000000,,,0x0\" allowed_cpuset=\"0x02000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"122\" cpuset=\"0x04000000,,,0x0\" complete_cpuset=\"0x04000000,,,0x0\" online_cpuset=\"0x04000000,,,0x0\" allowed_cpuset=\"0x04000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"123\" cpuset=\"0x08000000,,,0x0\" complete_cpuset=\"0x08000000,,,0x0\" online_cpuset=\"0x08000000,,,0x0\" allowed_cpuset=\"0x08000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0xf0000000,,,0x0\" complete_cpuset=\"0xf0000000,,,0x0\" online_cpuset=\"0xf0000000,,,0x0\" allowed_cpuset=\"0xf0000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2084\" cpuset=\"0xf0000000,,,0x0\" complete_cpuset=\"0xf0000000,,,0x0\" online_cpuset=\"0xf0000000,,,0x0\" allowed_cpuset=\"0xf0000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"124\" cpuset=\"0x10000000,,,0x0\" complete_cpuset=\"0x10000000,,,0x0\" online_cpuset=\"0x10000000,,,0x0\" allowed_cpuset=\"0x10000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"125\" cpuset=\"0x20000000,,,0x0\" complete_cpuset=\"0x20000000,,,0x0\" online_cpuset=\"0x20000000,,,0x0\" allowed_cpuset=\"0x20000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"126\" cpuset=\"0x40000000,,,0x0\" complete_cpuset=\"0x40000000,,,0x0\" online_cpuset=\"0x40000000,,,0x0\" allowed_cpuset=\"0x40000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"127\" cpuset=\"0x80000000,,,0x0\" complete_cpuset=\"0x80000000,,,0x0\" online_cpuset=\"0x80000000,,,0x0\" allowed_cpuset=\"0x80000000,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x000000ff,,,,0x0\" complete_cpuset=\"0x000000ff,,,,0x0\" online_cpuset=\"0x000000ff,,,,0x0\" allowed_cpuset=\"0x000000ff,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x000000ff,,,,0x0\" complete_cpuset=\"0x000000ff,,,,0x0\" online_cpuset=\"0x000000ff,,,,0x0\" allowed_cpuset=\"0x000000ff,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0000000f,,,,0x0\" complete_cpuset=\"0x0000000f,,,,0x0\" online_cpuset=\"0x0000000f,,,,0x0\" allowed_cpuset=\"0x0000000f,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2088\" cpuset=\"0x0000000f,,,,0x0\" complete_cpuset=\"0x0000000f,,,,0x0\" online_cpuset=\"0x0000000f,,,,0x0\" allowed_cpuset=\"0x0000000f,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"128\" cpuset=\"0x00000001,,,,0x0\" complete_cpuset=\"0x00000001,,,,0x0\" online_cpuset=\"0x00000001,,,,0x0\" allowed_cpuset=\"0x00000001,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"129\" cpuset=\"0x00000002,,,,0x0\" complete_cpuset=\"0x00000002,,,,0x0\" online_cpuset=\"0x00000002,,,,0x0\" allowed_cpuset=\"0x00000002,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"130\" cpuset=\"0x00000004,,,,0x0\" complete_cpuset=\"0x00000004,,,,0x0\" online_cpuset=\"0x00000004,,,,0x0\" allowed_cpuset=\"0x00000004,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"131\" cpuset=\"0x00000008,,,,0x0\" complete_cpuset=\"0x00000008,,,,0x0\" online_cpuset=\"0x00000008,,,,0x0\" allowed_cpuset=\"0x00000008,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x000000f0,,,,0x0\" complete_cpuset=\"0x000000f0,,,,0x0\" online_cpuset=\"0x000000f0,,,,0x0\" allowed_cpuset=\"0x000000f0,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2092\" cpuset=\"0x000000f0,,,,0x0\" complete_cpuset=\"0x000000f0,,,,0x0\" online_cpuset=\"0x000000f0,,,,0x0\" allowed_cpuset=\"0x000000f0,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"132\" cpuset=\"0x00000010,,,,0x0\" complete_cpuset=\"0x00000010,,,,0x0\" online_cpuset=\"0x00000010,,,,0x0\" allowed_cpuset=\"0x00000010,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"133\" cpuset=\"0x00000020,,,,0x0\" complete_cpuset=\"0x00000020,,,,0x0\" online_cpuset=\"0x00000020,,,,0x0\" allowed_cpuset=\"0x00000020,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"134\" cpuset=\"0x00000040,,,,0x0\" complete_cpuset=\"0x00000040,,,,0x0\" online_cpuset=\"0x00000040,,,,0x0\" allowed_cpuset=\"0x00000040,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"135\" cpuset=\"0x00000080,,,,0x0\" complete_cpuset=\"0x00000080,,,,0x0\" online_cpuset=\"0x00000080,,,,0x0\" allowed_cpuset=\"0x00000080,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x00000f00,,,,0x0\" complete_cpuset=\"0x00000f00,,,,0x0\" online_cpuset=\"0x00000f00,,,,0x0\" allowed_cpuset=\"0x00000f00,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x00000f00,,,,0x0\" complete_cpuset=\"0x00000f00,,,,0x0\" online_cpuset=\"0x00000f00,,,,0x0\" allowed_cpuset=\"0x00000f00,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x00000f00,,,,0x0\" complete_cpuset=\"0x00000f00,,,,0x0\" online_cpuset=\"0x00000f00,,,,0x0\" allowed_cpuset=\"0x00000f00,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2100\" cpuset=\"0x00000f00,,,,0x0\" complete_cpuset=\"0x00000f00,,,,0x0\" online_cpuset=\"0x00000f00,,,,0x0\" allowed_cpuset=\"0x00000f00,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"136\" cpuset=\"0x00000100,,,,0x0\" complete_cpuset=\"0x00000100,,,,0x0\" online_cpuset=\"0x00000100,,,,0x0\" allowed_cpuset=\"0x00000100,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"137\" cpuset=\"0x00000200,,,,0x0\" complete_cpuset=\"0x00000200,,,,0x0\" online_cpuset=\"0x00000200,,,,0x0\" allowed_cpuset=\"0x00000200,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"138\" cpuset=\"0x00000400,,,,0x0\" complete_cpuset=\"0x00000400,,,,0x0\" online_cpuset=\"0x00000400,,,,0x0\" allowed_cpuset=\"0x00000400,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"139\" cpuset=\"0x00000800,,,,0x0\" complete_cpuset=\"0x00000800,,,,0x0\" online_cpuset=\"0x00000800,,,,0x0\" allowed_cpuset=\"0x00000800,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x000ff000,,,,0x0\" complete_cpuset=\"0x000ff000,,,,0x0\" online_cpuset=\"0x000ff000,,,,0x0\" allowed_cpuset=\"0x000ff000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x000ff000,,,,0x0\" complete_cpuset=\"0x000ff000,,,,0x0\" online_cpuset=\"0x000ff000,,,,0x0\" allowed_cpuset=\"0x000ff000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0000f000,,,,0x0\" complete_cpuset=\"0x0000f000,,,,0x0\" online_cpuset=\"0x0000f000,,,,0x0\" allowed_cpuset=\"0x0000f000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2104\" cpuset=\"0x0000f000,,,,0x0\" complete_cpuset=\"0x0000f000,,,,0x0\" online_cpuset=\"0x0000f000,,,,0x0\" allowed_cpuset=\"0x0000f000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"140\" cpuset=\"0x00001000,,,,0x0\" complete_cpuset=\"0x00001000,,,,0x0\" online_cpuset=\"0x00001000,,,,0x0\" allowed_cpuset=\"0x00001000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"141\" cpuset=\"0x00002000,,,,0x0\" complete_cpuset=\"0x00002000,,,,0x0\" online_cpuset=\"0x00002000,,,,0x0\" allowed_cpuset=\"0x00002000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"142\" cpuset=\"0x00004000,,,,0x0\" complete_cpuset=\"0x00004000,,,,0x0\" online_cpuset=\"0x00004000,,,,0x0\" allowed_cpuset=\"0x00004000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"143\" cpuset=\"0x00008000,,,,0x0\" complete_cpuset=\"0x00008000,,,,0x0\" online_cpuset=\"0x00008000,,,,0x0\" allowed_cpuset=\"0x00008000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x000f0000,,,,0x0\" complete_cpuset=\"0x000f0000,,,,0x0\" online_cpuset=\"0x000f0000,,,,0x0\" allowed_cpuset=\"0x000f0000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2108\" cpuset=\"0x000f0000,,,,0x0\" complete_cpuset=\"0x000f0000,,,,0x0\" online_cpuset=\"0x000f0000,,,,0x0\" allowed_cpuset=\"0x000f0000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"144\" cpuset=\"0x00010000,,,,0x0\" complete_cpuset=\"0x00010000,,,,0x0\" online_cpuset=\"0x00010000,,,,0x0\" allowed_cpuset=\"0x00010000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"145\" cpuset=\"0x00020000,,,,0x0\" complete_cpuset=\"0x00020000,,,,0x0\" online_cpuset=\"0x00020000,,,,0x0\" allowed_cpuset=\"0x00020000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"146\" cpuset=\"0x00040000,,,,0x0\" complete_cpuset=\"0x00040000,,,,0x0\" online_cpuset=\"0x00040000,,,,0x0\" allowed_cpuset=\"0x00040000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"147\" cpuset=\"0x00080000,,,,0x0\" complete_cpuset=\"0x00080000,,,,0x0\" online_cpuset=\"0x00080000,,,,0x0\" allowed_cpuset=\"0x00080000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x00f00000,,,,0x0\" complete_cpuset=\"0x00f00000,,,,0x0\" online_cpuset=\"0x00f00000,,,,0x0\" allowed_cpuset=\"0x00f00000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x00f00000,,,,0x0\" complete_cpuset=\"0x00f00000,,,,0x0\" online_cpuset=\"0x00f00000,,,,0x0\" allowed_cpuset=\"0x00f00000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x00f00000,,,,0x0\" complete_cpuset=\"0x00f00000,,,,0x0\" online_cpuset=\"0x00f00000,,,,0x0\" allowed_cpuset=\"0x00f00000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2112\" cpuset=\"0x00f00000,,,,0x0\" complete_cpuset=\"0x00f00000,,,,0x0\" online_cpuset=\"0x00f00000,,,,0x0\" allowed_cpuset=\"0x00f00000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"148\" cpuset=\"0x00100000,,,,0x0\" complete_cpuset=\"0x00100000,,,,0x0\" online_cpuset=\"0x00100000,,,,0x0\" allowed_cpuset=\"0x00100000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"149\" cpuset=\"0x00200000,,,,0x0\" complete_cpuset=\"0x00200000,,,,0x0\" online_cpuset=\"0x00200000,,,,0x0\" allowed_cpuset=\"0x00200000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"150\" cpuset=\"0x00400000,,,,0x0\" complete_cpuset=\"0x00400000,,,,0x0\" online_cpuset=\"0x00400000,,,,0x0\" allowed_cpuset=\"0x00400000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"151\" cpuset=\"0x00800000,,,,0x0\" complete_cpuset=\"0x00800000,,,,0x0\" online_cpuset=\"0x00800000,,,,0x0\" allowed_cpuset=\"0x00800000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0xff000000,,,,0x0\" complete_cpuset=\"0xff000000,,,,0x0\" online_cpuset=\"0xff000000,,,,0x0\" allowed_cpuset=\"0xff000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0xff000000,,,,0x0\" complete_cpuset=\"0xff000000,,,,0x0\" online_cpuset=\"0xff000000,,,,0x0\" allowed_cpuset=\"0xff000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0f000000,,,,0x0\" complete_cpuset=\"0x0f000000,,,,0x0\" online_cpuset=\"0x0f000000,,,,0x0\" allowed_cpuset=\"0x0f000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2120\" cpuset=\"0x0f000000,,,,0x0\" complete_cpuset=\"0x0f000000,,,,0x0\" online_cpuset=\"0x0f000000,,,,0x0\" allowed_cpuset=\"0x0f000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"152\" cpuset=\"0x01000000,,,,0x0\" complete_cpuset=\"0x01000000,,,,0x0\" online_cpuset=\"0x01000000,,,,0x0\" allowed_cpuset=\"0x01000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"153\" cpuset=\"0x02000000,,,,0x0\" complete_cpuset=\"0x02000000,,,,0x0\" online_cpuset=\"0x02000000,,,,0x0\" allowed_cpuset=\"0x02000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"154\" cpuset=\"0x04000000,,,,0x0\" complete_cpuset=\"0x04000000,,,,0x0\" online_cpuset=\"0x04000000,,,,0x0\" allowed_cpuset=\"0x04000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"155\" cpuset=\"0x08000000,,,,0x0\" complete_cpuset=\"0x08000000,,,,0x0\" online_cpuset=\"0x08000000,,,,0x0\" allowed_cpuset=\"0x08000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0xf0000000,,,,0x0\" complete_cpuset=\"0xf0000000,,,,0x0\" online_cpuset=\"0xf0000000,,,,0x0\" allowed_cpuset=\"0xf0000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2124\" cpuset=\"0xf0000000,,,,0x0\" complete_cpuset=\"0xf0000000,,,,0x0\" online_cpuset=\"0xf0000000,,,,0x0\" allowed_cpuset=\"0xf0000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"156\" cpuset=\"0x10000000,,,,0x0\" complete_cpuset=\"0x10000000,,,,0x0\" online_cpuset=\"0x10000000,,,,0x0\" allowed_cpuset=\"0x10000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"157\" cpuset=\"0x20000000,,,,0x0\" complete_cpuset=\"0x20000000,,,,0x0\" online_cpuset=\"0x20000000,,,,0x0\" allowed_cpuset=\"0x20000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"158\" cpuset=\"0x40000000,,,,0x0\" complete_cpuset=\"0x40000000,,,,0x0\" online_cpuset=\"0x40000000,,,,0x0\" allowed_cpuset=\"0x40000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"159\" cpuset=\"0x80000000,,,,0x0\" complete_cpuset=\"0x80000000,,,,0x0\" online_cpuset=\"0x80000000,,,,0x0\" allowed_cpuset=\"0x80000000,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x000000ff,,,,,0x0\" complete_cpuset=\"0x000000ff,,,,,0x0\" online_cpuset=\"0x000000ff,,,,,0x0\" allowed_cpuset=\"0x000000ff,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x000000ff,,,,,0x0\" complete_cpuset=\"0x000000ff,,,,,0x0\" online_cpuset=\"0x000000ff,,,,,0x0\" allowed_cpuset=\"0x000000ff,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x0000000f,,,,,0x0\" complete_cpuset=\"0x0000000f,,,,,0x0\" online_cpuset=\"0x0000000f,,,,,0x0\" allowed_cpuset=\"0x0000000f,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2128\" cpuset=\"0x0000000f,,,,,0x0\" complete_cpuset=\"0x0000000f,,,,,0x0\" online_cpuset=\"0x0000000f,,,,,0x0\" allowed_cpuset=\"0x0000000f,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"160\" cpuset=\"0x00000001,,,,,0x0\" complete_cpuset=\"0x00000001,,,,,0x0\" online_cpuset=\"0x00000001,,,,,0x0\" allowed_cpuset=\"0x00000001,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"161\" cpuset=\"0x00000002,,,,,0x0\" complete_cpuset=\"0x00000002,,,,,0x0\" online_cpuset=\"0x00000002,,,,,0x0\" allowed_cpuset=\"0x00000002,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"162\" cpuset=\"0x00000004,,,,,0x0\" complete_cpuset=\"0x00000004,,,,,0x0\" online_cpuset=\"0x00000004,,,,,0x0\" allowed_cpuset=\"0x00000004,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"163\" cpuset=\"0x00000008,,,,,0x0\" complete_cpuset=\"0x00000008,,,,,0x0\" online_cpuset=\"0x00000008,,,,,0x0\" allowed_cpuset=\"0x00000008,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x000000f0,,,,,0x0\" complete_cpuset=\"0x000000f0,,,,,0x0\" online_cpuset=\"0x000000f0,,,,,0x0\" allowed_cpuset=\"0x000000f0,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2132\" cpuset=\"0x000000f0,,,,,0x0\" complete_cpuset=\"0x000000f0,,,,,0x0\" online_cpuset=\"0x000000f0,,,,,0x0\" allowed_cpuset=\"0x000000f0,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"164\" cpuset=\"0x00000010,,,,,0x0\" complete_cpuset=\"0x00000010,,,,,0x0\" online_cpuset=\"0x00000010,,,,,0x0\" allowed_cpuset=\"0x00000010,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"165\" cpuset=\"0x00000020,,,,,0x0\" complete_cpuset=\"0x00000020,,,,,0x0\" online_cpuset=\"0x00000020,,,,,0x0\" allowed_cpuset=\"0x00000020,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"166\" cpuset=\"0x00000040,,,,,0x0\" complete_cpuset=\"0x00000040,,,,,0x0\" online_cpuset=\"0x00000040,,,,,0x0\" allowed_cpuset=\"0x00000040,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"167\" cpuset=\"0x00000080,,,,,0x0\" complete_cpuset=\"0x00000080,,,,,0x0\" online_cpuset=\"0x00000080,,,,,0x0\" allowed_cpuset=\"0x00000080,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
          <object type=\"Cache\" cpuset=\"0x0000ff00,,,,,0x0\" complete_cpuset=\"0x0000ff00,,,,,0x0\" online_cpuset=\"0x0000ff00,,,,,0x0\" allowed_cpuset=\"0x0000ff00,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"10485760\" depth=\"3\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
            <object type=\"Cache\" cpuset=\"0x0000ff00,,,,,0x0\" complete_cpuset=\"0x0000ff00,,,,,0x0\" online_cpuset=\"0x0000ff00,,,,,0x0\" allowed_cpuset=\"0x0000ff00,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"524288\" depth=\"2\" cache_linesize=\"0\" cache_associativity=\"0\" cache_type=\"0\">\n\
              <object type=\"Cache\" cpuset=\"0x00000f00,,,,,0x0\" complete_cpuset=\"0x00000f00,,,,,0x0\" online_cpuset=\"0x00000f00,,,,,0x0\" allowed_cpuset=\"0x00000f00,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2136\" cpuset=\"0x00000f00,,,,,0x0\" complete_cpuset=\"0x00000f00,,,,,0x0\" online_cpuset=\"0x00000f00,,,,,0x0\" allowed_cpuset=\"0x00000f00,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"168\" cpuset=\"0x00000100,,,,,0x0\" complete_cpuset=\"0x00000100,,,,,0x0\" online_cpuset=\"0x00000100,,,,,0x0\" allowed_cpuset=\"0x00000100,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"169\" cpuset=\"0x00000200,,,,,0x0\" complete_cpuset=\"0x00000200,,,,,0x0\" online_cpuset=\"0x00000200,,,,,0x0\" allowed_cpuset=\"0x00000200,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"170\" cpuset=\"0x00000400,,,,,0x0\" complete_cpuset=\"0x00000400,,,,,0x0\" online_cpuset=\"0x00000400,,,,,0x0\" allowed_cpuset=\"0x00000400,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"171\" cpuset=\"0x00000800,,,,,0x0\" complete_cpuset=\"0x00000800,,,,,0x0\" online_cpuset=\"0x00000800,,,,,0x0\" allowed_cpuset=\"0x00000800,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
              <object type=\"Cache\" cpuset=\"0x0000f000,,,,,0x0\" complete_cpuset=\"0x0000f000,,,,,0x0\" online_cpuset=\"0x0000f000,,,,,0x0\" allowed_cpuset=\"0x0000f000,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\" cache_size=\"32768\" depth=\"1\" cache_linesize=\"128\" cache_associativity=\"32\" cache_type=\"1\">\n\
                <object type=\"Core\" os_index=\"2140\" cpuset=\"0x0000f000,,,,,0x0\" complete_cpuset=\"0x0000f000,,,,,0x0\" online_cpuset=\"0x0000f000,,,,,0x0\" allowed_cpuset=\"0x0000f000,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\">\n\
                  <object type=\"PU\" os_index=\"172\" cpuset=\"0x00001000,,,,,0x0\" complete_cpuset=\"0x00001000,,,,,0x0\" online_cpuset=\"0x00001000,,,,,0x0\" allowed_cpuset=\"0x00001000,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"173\" cpuset=\"0x00002000,,,,,0x0\" complete_cpuset=\"0x00002000,,,,,0x0\" online_cpuset=\"0x00002000,,,,,0x0\" allowed_cpuset=\"0x00002000,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"174\" cpuset=\"0x00004000,,,,,0x0\" complete_cpuset=\"0x00004000,,,,,0x0\" online_cpuset=\"0x00004000,,,,,0x0\" allowed_cpuset=\"0x00004000,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                  <object type=\"PU\" os_index=\"175\" cpuset=\"0x00008000,,,,,0x0\" complete_cpuset=\"0x00008000,,,,,0x0\" online_cpuset=\"0x00008000,,,,,0x0\" allowed_cpuset=\"0x00008000,,,,,0x0\" nodeset=\"0x00000100\" complete_nodeset=\"0x00000100\" allowed_nodeset=\"0x00000100\"/>\n\
                </object>\n\
              </object>\n\
            </object>\n\
          </object>\n\
        </object>\n\
        <object type=\"Bridge\" os_index=\"9\" bridge_type=\"0-1\" depth=\"0\" bridge_pci=\"0033:[00-01]\">\n\
          <object type=\"PCIDev\" os_index=\"53481472\" name=\"Mellanox Technologies MT28800 Family [ConnectX-5 Ex]\" pci_busid=\"0033:01:00.0\" pci_type=\"0207 [15b3:1019] [1014:0617] 00\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Mellanox Technologies\"/>\n\
            <info name=\"PCIDevice\" value=\"MT28800 Family [ConnectX-5 Ex]\"/>\n\
            <object type=\"OSDev\" name=\"hsi2\" osdev_type=\"2\">\n\
              <info name=\"Address\" value=\"00:00:14:87:fe:80:00:00:00:00:00:00:ec:0d:9a:03:00:ca:a4:1a\"/>\n\
              <info name=\"Port\" value=\"1\"/>\n\
            </object>\n\
            <object type=\"OSDev\" name=\"mlx5_2\" osdev_type=\"3\">\n\
              <info name=\"NodeGUID\" value=\"ec0d:9a03:00ca:a41a\"/>\n\
              <info name=\"SysImageGUID\" value=\"ec0d:9a03:00ca:a418\"/>\n\
              <info name=\"Port1State\" value=\"4\"/>\n\
              <info name=\"Port1LID\" value=\"0x335d\"/>\n\
              <info name=\"Port1LMC\" value=\"0\"/>\n\
              <info name=\"Port1GID0\" value=\"fe80:0000:0000:0000:ec0d:9a03:00ca:a41a\"/>\n\
            </object>\n\
          </object>\n\
          <object type=\"PCIDev\" os_index=\"53481473\" name=\"Mellanox Technologies MT28800 Family [ConnectX-5 Ex]\" pci_busid=\"0033:01:00.1\" pci_type=\"0207 [15b3:1019] [1014:0617] 00\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"Mellanox Technologies\"/>\n\
            <info name=\"PCIDevice\" value=\"MT28800 Family [ConnectX-5 Ex]\"/>\n\
            <object type=\"OSDev\" name=\"hsi3\" osdev_type=\"2\">\n\
              <info name=\"Address\" value=\"00:00:1c:87:fe:80:00:00:00:00:00:00:ec:0d:9a:03:00:ca:a4:1b\"/>\n\
              <info name=\"Port\" value=\"1\"/>\n\
            </object>\n\
            <object type=\"OSDev\" name=\"mlx5_3\" osdev_type=\"3\">\n\
              <info name=\"NodeGUID\" value=\"ec0d:9a03:00ca:a41b\"/>\n\
              <info name=\"SysImageGUID\" value=\"ec0d:9a03:00ca:a418\"/>\n\
              <info name=\"Port1State\" value=\"4\"/>\n\
              <info name=\"Port1LID\" value=\"0x3333\"/>\n\
              <info name=\"Port1LMC\" value=\"0\"/>\n\
              <info name=\"Port1GID0\" value=\"fe80:0000:0000:0000:ec0d:9a03:00ca:a41b\"/>\n\
            </object>\n\
          </object>\n\
        </object>\n\
        <object type=\"Bridge\" os_index=\"11\" bridge_type=\"0-1\" depth=\"0\" bridge_pci=\"0035:[00-09]\">\n\
          <object type=\"PCIDev\" os_index=\"55586816\" name=\"NVIDIA Corporation GV100GL [Tesla V100 SXM2]\" pci_busid=\"0035:03:00.0\" pci_type=\"0302 [10de:1db1] [10de:1212] a1\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"NVIDIA Corporation\"/>\n\
            <info name=\"PCIDevice\" value=\"GV100GL [Tesla V100 SXM2]\"/>\n\
            <object type=\"OSDev\" name=\"card2\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"renderD130\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"cuda2\" osdev_type=\"5\">\n\
              <info name=\"CoProcType\" value=\"CUDA\"/>\n\
              <info name=\"Backend\" value=\"CUDA\"/>\n\
              <info name=\"GPUVendor\" value=\"NVIDIA Corporation\"/>\n\
              <info name=\"GPUModel\" value=\"Tesla V100-SXM2-16GB\"/>\n\
              <info name=\"CUDAGlobalMemorySize\" value=\"16515072\"/>\n\
              <info name=\"CUDAL2CacheSize\" value=\"6144\"/>\n\
              <info name=\"CUDAMultiProcessors\" value=\"80\"/>\n\
              <info name=\"CUDACoresPerMP\" value=\"64\"/>\n\
              <info name=\"CUDASharedMemorySizePerMP\" value=\"48\"/>\n\
            </object>\n\
          </object>\n\
          <object type=\"PCIDev\" os_index=\"55590912\" name=\"NVIDIA Corporation GV100GL [Tesla V100 SXM2]\" pci_busid=\"0035:04:00.0\" pci_type=\"0302 [10de:1db1] [10de:1212] a1\" pci_link_speed=\"0.000000\">\n\
            <info name=\"PCIVendor\" value=\"NVIDIA Corporation\"/>\n\
            <info name=\"PCIDevice\" value=\"GV100GL [Tesla V100 SXM2]\"/>\n\
            <object type=\"OSDev\" name=\"card3\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"renderD131\" osdev_type=\"1\"/>\n\
            <object type=\"OSDev\" name=\"cuda3\" osdev_type=\"5\">\n\
              <info name=\"CoProcType\" value=\"CUDA\"/>\n\
              <info name=\"Backend\" value=\"CUDA\"/>\n\
              <info name=\"GPUVendor\" value=\"NVIDIA Corporation\"/>\n\
              <info name=\"GPUModel\" value=\"Tesla V100-SXM2-16GB\"/>\n\
              <info name=\"CUDAGlobalMemorySize\" value=\"16515072\"/>\n\
              <info name=\"CUDAL2CacheSize\" value=\"6144\"/>\n\
              <info name=\"CUDAMultiProcessors\" value=\"80\"/>\n\
              <info name=\"CUDACoresPerMP\" value=\"64\"/>\n\
              <info name=\"CUDASharedMemorySizePerMP\" value=\"48\"/>\n\
            </object>\n\
          </object>\n\
        </object>\n\
      </object>\n\
    </object>\n\
    <object type=\"NUMANode\" os_index=\"252\" cpuset=\"0x0\" complete_cpuset=\"0x0\" online_cpuset=\"0x0\" allowed_cpuset=\"0x0\" nodeset=\"0x10000000,,,,,,,0x0\" complete_nodeset=\"0x10000000,,,,,,,0x0\" allowed_nodeset=\"0x10000000,,,,,,,0x0\" local_memory=\"16911433728\">\n\
      <page_type size=\"65536\" count=\"258048\"/>\n\
      <page_type size=\"2097152\" count=\"0\"/>\n\
      <page_type size=\"1073741824\" count=\"0\"/>\n\
    </object>\n\
    <object type=\"NUMANode\" os_index=\"253\" cpuset=\"0x0\" complete_cpuset=\"0x0\" online_cpuset=\"0x0\" allowed_cpuset=\"0x0\" nodeset=\"0x20000000,,,,,,,0x0\" complete_nodeset=\"0x20000000,,,,,,,0x0\" allowed_nodeset=\"0x20000000,,,,,,,0x0\" local_memory=\"16911433728\">\n\
      <page_type size=\"65536\" count=\"258048\"/>\n\
      <page_type size=\"2097152\" count=\"0\"/>\n\
      <page_type size=\"1073741824\" count=\"0\"/>\n\
    </object>\n\
    <object type=\"NUMANode\" os_index=\"254\" cpuset=\"0x0\" complete_cpuset=\"0x0\" online_cpuset=\"0x0\" allowed_cpuset=\"0x0\" nodeset=\"0x40000000,,,,,,,0x0\" complete_nodeset=\"0x40000000,,,,,,,0x0\" allowed_nodeset=\"0x40000000,,,,,,,0x0\" local_memory=\"16911433728\">\n\
      <page_type size=\"65536\" count=\"258048\"/>\n\
      <page_type size=\"2097152\" count=\"0\"/>\n\
      <page_type size=\"1073741824\" count=\"0\"/>\n\
    </object>\n\
    <object type=\"NUMANode\" os_index=\"255\" cpuset=\"0x0\" complete_cpuset=\"0x0\" online_cpuset=\"0x0\" allowed_cpuset=\"0x0\" nodeset=\"0x80000000,,,,,,,0x0\" complete_nodeset=\"0x80000000,,,,,,,0x0\" allowed_nodeset=\"0x80000000,,,,,,,0x0\" local_memory=\"16911433728\">\n\
      <page_type size=\"65536\" count=\"258048\"/>\n\
      <page_type size=\"2097152\" count=\"0\"/>\n\
      <page_type size=\"1073741824\" count=\"0\"/>\n\
    </object>\n\
  </object>\n\
</topology>";

void test_hwloc (const char *xml)
{
    char *s;
    struct hostlist *hl;
    json_t *R;
    struct rlist *rl = rlist_from_hwloc (0, xml);

    if (!rl)
        BAIL_OUT ("rlist_from_hwloc failed!");
    if (!(hl = rlist_nodelist (rl)))
        BAIL_OUT ("rlist_nodelist failed");

    ok (rlist_nnodes (rl) == 1 && rl->total > 0,
        "rlist_from_hwloc() was able to gather hwloc from %s",
        xml ? "xml" : "local");

    s = rlist_dumps (rl);
    diag ("%s", s);
    free (s);

    R = rlist_to_R (rl);
    s = json_dumps (R, JSON_COMPACT);
    json_decref (R);
    diag ("%s", s);
    free (s);

    ok (hostlist_count (hl) == 1,
        "rlist_nodelist works on rlist gathered via hwloc");

    s = hostlist_encode (hl);
    diag ("hosts=%s", s);
    free (s);

    rlist_destroy (rl);
    hostlist_destroy (hl);
}

void test_xml ()
{
    flux_error_t error;
    struct rlist *rl1 = NULL;
    struct rlist *rl2 = NULL;
    char *s1 = NULL;
    char *s2 = NULL;
    char *xml = rhwloc_local_topology_xml ();
    if (!xml)
        BAIL_OUT ("rhwloc_local_topology_xml failed!");
    pass ("rhwloc_topology_xml");

    /*  rlist from local XML and rlist from local directly should match */

    rl1 = rlist_from_hwloc (0, NULL);
    rl2 = rlist_from_hwloc (0, xml);
    if (!rl1 || !rl2)
        BAIL_OUT ("rlist_from_hwloc failed!");

    s1 = rlist_dumps (rl1);
    s2 = rlist_dumps (rl2);
    diag ("local hwloc got %s, local XML got %s", s1, s2);
    free (s1);
    free (s2);

    ok (rlist_verify (&error, rl1, rl2) == 0,
        "rlist from local hwloc and rlist from local XML match: %s",
        error.text);

    rlist_destroy (rl1);
    rlist_destroy (rl2);
    free (xml);
}

int main (int ac, char *av[])
{
    plan (NO_PLAN);

    test_hwloc (NULL);
    test_hwloc (xml1);
    test_xml ();

    done_testing ();
}

/* vi: ts=4 sw=4 expandtab
 */
