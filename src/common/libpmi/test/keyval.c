/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/common/libtap/tap.h"
#include "src/common/libpmi/keyval.h"

#include <string.h>
#include <ctype.h>

static char *valid[] = {
    "key1=val1",
    "key1=val1 ",
    "key1=val1\n",
    "key1=val1 key2=val2\n",
    "key1=val1  key2=val2\tkey3=42\n",
    "key1=val1 key2=val2 key3=42 key4=-42\n",  // 5
    "key1=val1 key2=val2 key3=42 key4=-42 key5=foo=bar key6=baz\n",
    "key1=val1 key2=val2 key3=42 key4=-42 key5=foo=bar key6=baz key7=x y z=\n",
    "key1=42",
    "fookey1=val1",
    NULL,  // 10
};

/* Some PMI-1 messages from flux-framework/flux-core#398 and #709 */
static char *pmi[] = {
    "cmd=init pmi_version=1 pmi_subversion=1\n",
    "cmd=response_to_init rc=0 pmi_version=1 pmi_subversion=1\n",
    "cmd=get_maxes\n",
    "cmd=maxes rc=0 kvsname_max=256 keylen_max=256 vallen_max=256\n",
    "cmd=get_universe_size\n",
    "cmd=universe_size rc=0 size=2\n",  // 5
    "cmd=get_appnum\n",
    "cmd=appnum rc=0 appnum=0\n",
    "cmd=barrier_in\n",
    "cmd=barrier_out rc=0\n",
    "cmd=finalize\n",  // 10
    "cmd=finalize_ack rc=0\n",
    "cmd=get_my_kvsname\n",
    "cmd=my_kvsname rc=0 kvsname=lwj.1.pmi\n",
    "cmd=put kvsname=lwj.1.pmi key=PM value=/dev/shm/mpich_shar_tmpYbGKbb\n",
    "cmd=put_result rc=0 msg=success\n",  // 15
    "cmd=get kvsname=lwj.1.pmi key=sh\n",
    "cmd=get_result rc=0 msg=success value=/dev/shm/mpich_shar_tmpYbGKbb\n",
    "cmd=publish_name service=zz port=merp42\n",
    "cmd=publish_result rc=0 info=ok\n",
    "cmd=lookup_name service=zz\n",  // 20
    "cmd=lookup_result rc=0 info=ok port=merp42\n",
    "cmd=unpublish_name service=zz\n",
    "cmd=unpublish_result rc=0 info=ok\n",
    NULL,
};

static char *spawn[] = {
    "mcmd=spawn\n",
    "nprocs=2\n",
    "execname=workprog\n",
    "totspawns=2\n",
    "spawnssofar=0\n",
    "arg0=workprog\n",  // 5
    "arg1=--do-something=yes\n",
    "arg2=-X\n",
    "arg3=inputdeck\n",
    "argcnt=4\n",
    "preput_num=1\n",  // 10
    "preput_key_0=foo\n",
    "preput_val_0=bar\n",
    "info_num=1\n",
    "info_key_0=baz\n",
    "info_val_0=zurn\n",  // 15
    "endcmd\n",
    "cmd=spawn_result rc=0 errcodes=0,0\n",
    NULL,
};

int main (int argc, char **argv)
{
    char val[42];
    int i;
    unsigned int ui;

    plan (NO_PLAN);

    ok (keyval_parse_word (valid[0], "key1", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "val1"),
        "keyval_parse_word parsed the first key");
    ok (keyval_parse_word (valid[1], "key1", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "val1"),
        "keyval_parse_word parsed the first word, ignoring trailing space");
    ok (keyval_parse_word (valid[2], "key1", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "val1"),
        "keyval_parse_word parsed the first word, ignoring trailing newline");
    ok (keyval_parse_word (valid[2], "noexist", val, sizeof (val)) == EKV_NOKEY,
        "keyval_parse_word failed on nonexistent key");
    ok (keyval_parse_word (valid[3], "key2", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "val2"),
        "keyval_parse_word parsed the second key");
    ok (keyval_parse_uint (valid[4], "key3", &ui) == EKV_SUCCESS && ui == 42,
        "keyval_parse_uint worked");
    ok (keyval_parse_int (valid[4], "key3", &i) == EKV_SUCCESS && i == 42,
        "keyval_parse_int worked on positive integer");
    ok (keyval_parse_int (valid[5], "key4", &i) == EKV_SUCCESS && i == -42,
        "keyval_parse_int worked on negative integer");
    ok (keyval_parse_word (valid[6], "key5", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "foo=bar"),
        "keyval_parse_word handled value containing an equals");
    ok (keyval_parse_word (valid[6], "key6", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "baz"),
        "keyval_parse_word parsed word following value containing an equals");
    ok (keyval_parse_string (valid[7], "key7", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "x y z="),
        "keyval_parse_string parsed string containing space and equals");
    ok (keyval_parse_int (valid[8], "key1", &i) == EKV_SUCCESS && i == 42,
        "keyval_parse_int parsed int not followed by white space");
    ok (keyval_parse_word (valid[9], "key1", val, sizeof (val)) == EKV_NOKEY,
        "keyval_parse_word failed on key that is substring of another key");

    /* PMI-1 strings
     */

    ok (keyval_parse_word (pmi[0], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "init")
            && keyval_parse_uint (pmi[0], "pmi_version", &ui) == EKV_SUCCESS && ui == 1
            && keyval_parse_uint (pmi[0], "pmi_subversion", &ui) == EKV_SUCCESS
            && ui == 1,
        "parsed pmi-1 init request");
    ok (keyval_parse_word (pmi[1], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "response_to_init")
            && keyval_parse_int (pmi[1], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_uint (pmi[1], "pmi_version", &ui) == EKV_SUCCESS && ui == 1
            && keyval_parse_uint (pmi[1], "pmi_subversion", &ui) == EKV_SUCCESS
            && ui == 1,
        "parsed pmi-1 init response");
    ok (keyval_parse_word (pmi[2], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "get_maxes"),
        "parsed pmi-1 maxes request");
    ok (keyval_parse_word (pmi[3], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "maxes")
            && keyval_parse_int (pmi[3], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_uint (pmi[3], "kvsname_max", &ui) == EKV_SUCCESS
            && ui == 256 && keyval_parse_uint (pmi[3], "keylen_max", &ui) == EKV_SUCCESS
            && ui == 256 && keyval_parse_uint (pmi[3], "vallen_max", &ui) == EKV_SUCCESS
            && ui == 256,
        "parsed pmi-1 maxes response");
    ok (keyval_parse_word (pmi[4], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "get_universe_size"),
        "parsed pmi-1 universe_size request");
    ok (keyval_parse_word (pmi[5], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "universe_size")
            && keyval_parse_int (pmi[5], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_uint (pmi[5], "size", &ui) == EKV_SUCCESS && ui == 2,
        "parsed pmi-1 universe_size response");
    ok (keyval_parse_word (pmi[6], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "get_appnum"),
        "parsed pmi-1 appnum request");
    ok (keyval_parse_word (pmi[7], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "appnum")
            && keyval_parse_int (pmi[7], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_int (pmi[7], "appnum", &i) == EKV_SUCCESS && i == 0,
        "parsed pmi-1 appnum response");
    ok (keyval_parse_word (pmi[8], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "barrier_in"),
        "parsed pmi-1 barrier request");
    ok (keyval_parse_word (pmi[9], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "barrier_out")
            && keyval_parse_int (pmi[9], "rc", &i) == EKV_SUCCESS && i == 0,
        "parsed pmi-1 barrier response");
    ok (keyval_parse_word (pmi[10], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "finalize"),
        "parsed pmi-1 finalize request");
    ok (keyval_parse_word (pmi[11], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "finalize_ack")
            && keyval_parse_int (pmi[11], "rc", &i) == EKV_SUCCESS && i == 0,
        "parsed pmi-1 finalize response");
    ok (keyval_parse_word (pmi[12], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "get_my_kvsname"),
        "parsed pmi-1 kvsname request");
    ok (keyval_parse_word (pmi[13], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "my_kvsname")
            && keyval_parse_int (pmi[13], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_word (pmi[13], "kvsname", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "lwj.1.pmi"),
        "parsed pmi-1 kvsname response");
    ok (keyval_parse_word (pmi[14], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "put")
            && keyval_parse_word (pmi[14], "kvsname", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "lwj.1.pmi")
            && keyval_parse_word (pmi[14], "key", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "PM")
            && keyval_parse_string (pmi[14], "value", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "/dev/shm/mpich_shar_tmpYbGKbb"),
        "parsed pmi-1 put request");
    ok (keyval_parse_word (pmi[15], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "put_result") && keyval_parse_int (pmi[15], "rc", &i) == 0
            && i == EKV_SUCCESS
            && keyval_parse_string (pmi[15], "msg", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "success"),
        "parsed pmi-1 put response");
    ok (keyval_parse_word (pmi[16], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "get")
            && keyval_parse_word (pmi[16], "kvsname", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "lwj.1.pmi")
            && keyval_parse_word (pmi[16], "key", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "sh"),
        "parsed pmi-1 get request");
    ok (keyval_parse_word (pmi[17], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "get_result")
            && keyval_parse_int (pmi[17], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_word (pmi[17], "msg", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "success")
            && keyval_parse_string (pmi[17], "value", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "/dev/shm/mpich_shar_tmpYbGKbb"),
        "parsed pmi-1 get response");
    ok (keyval_parse_word (pmi[18], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "publish_name")
            && keyval_parse_word (pmi[18], "service", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "zz")
            && keyval_parse_word (pmi[18], "port", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "merp42"),
        "parsed pmi-1 publish request");
    ok (keyval_parse_word (pmi[19], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "publish_result")
            && keyval_parse_int (pmi[19], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_word (pmi[19], "info", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "ok"),
        "parsed pmi-1 publish response");
    ok (keyval_parse_word (pmi[20], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "lookup_name")
            && keyval_parse_word (pmi[20], "service", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "zz"),
        "parsed pmi-1 lookup request");
    ok (keyval_parse_word (pmi[21], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "lookup_result")
            && keyval_parse_int (pmi[21], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_word (pmi[21], "info", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "ok")
            && keyval_parse_word (pmi[21], "port", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "merp42"),
        "parsed pmi-1 lookup response");
    ok (keyval_parse_word (pmi[22], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "unpublish_name")
            && keyval_parse_word (pmi[22], "service", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "zz"),
        "parsed pmi-1 unpublish request");
    ok (keyval_parse_word (pmi[23], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "unpublish_result")
            && keyval_parse_int (pmi[23], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_word (pmi[23], "info", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "ok"),
        "parsed pmi-1 unpublish response");

    ok (keyval_parse_word (spawn[0], "mcmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "spawn"),
        "parsed pmi-1 spawn mcmd request");
    ok (keyval_parse_uint (spawn[1], "nprocs", &ui) == EKV_SUCCESS && ui == 2,
        "parsed pmi-1 spawn nprocs request");
    ok (keyval_parse_word (spawn[2], "execname", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "workprog"),
        "parsed pmi-1 spawn execname request");
    ok (keyval_parse_uint (spawn[3], "totspawns", &ui) == EKV_SUCCESS && ui == 2,
        "parsed pmi-1 spawn totspawns request");
    ok (keyval_parse_uint (spawn[4], "spawnssofar", &ui) == EKV_SUCCESS && ui == 0,
        "parsed pmi-1 spawn spawnssofar request");
    ok (keyval_parse_word (spawn[5], "arg0", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "workprog"),
        "parsed pmi-1 spawn arg0 request");
    ok (keyval_parse_word (spawn[6], "arg1", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "--do-something=yes"),
        "parsed pmi-1 spawn arg1 request");
    ok (keyval_parse_word (spawn[7], "arg2", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "-X"),
        "parsed pmi-1 spawn arg2 request");
    ok (keyval_parse_word (spawn[8], "arg3", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "inputdeck"),
        "parsed pmi-1 spawn arg3 request");
    ok (keyval_parse_uint (spawn[9], "argcnt", &ui) == EKV_SUCCESS && ui == 4,
        "parsed pmi-1 spawn argcnt request");
    ok (keyval_parse_uint (spawn[10], "preput_num", &ui) == EKV_SUCCESS && ui == 1,
        "parsed pmi-1 spawn preput_num request");
    ok (keyval_parse_word (spawn[11], "preput_key_0", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "foo"),
        "parsed pmi-1 spawn preput_key_0 request");
    ok (keyval_parse_word (spawn[12], "preput_val_0", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "bar"),
        "parsed pmi-1 spawn preput_val_0 request");
    ok (keyval_parse_uint (spawn[13], "info_num", &ui) == EKV_SUCCESS && ui == 1,
        "parsed pmi-1 spawn info_num request");
    ok (keyval_parse_word (spawn[14], "info_key_0", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "baz"),
        "parsed pmi-1 spawn info_key_0 request");
    ok (keyval_parse_word (spawn[15], "info_val_0", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "zurn"),
        "parsed pmi-1 spawn info_val_0 request");
    /* skip endcmd - we'll just strcmp that one */
    ok (keyval_parse_word (spawn[17], "cmd", val, sizeof (val)) == EKV_SUCCESS
            && !strcmp (val, "spawn_result")
            && keyval_parse_int (spawn[17], "rc", &i) == EKV_SUCCESS && i == 0
            && keyval_parse_word (spawn[17], "errcodes", val, sizeof (val))
                   == EKV_SUCCESS
            && !strcmp (val, "0,0"),
        "parsed pmi-1 spawn response");

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
