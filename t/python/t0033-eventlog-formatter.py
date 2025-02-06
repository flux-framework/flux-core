#!/usr/bin/env python3
###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import os
import time
import unittest

import subflux  # noqa: F401
from flux.eventlog import EventLogFormatter
from pycotap import TAPTestRunner

TEST_EVENTLOG = """\
{"timestamp":1738774194.078433,"name":"submit","context":{"userid":1001,"urgency":16,"flags":0,"version":1}}
{"timestamp":1738774194.0947378,"name":"validate"}
{"timestamp":1738774194.1073177,"name":"depend"}
{"timestamp":1738774194.107436,"name":"priority","context":{"priority":16}}
{"timestamp":1738774194.110116,"name":"alloc","context":{"annotations":{"sched":{"resource_summary":"rank[0-3]/core[0-7]"}}}}
{"timestamp":1738774194.1177957,"name":"start"}
{"timestamp":1738774194.1820674,"name":"finish","context":{"status":0}}
{"timestamp":1738774194.1853716,"name":"release","context":{"ranks":"all","final":true}}
{"timestamp":1738774194.1854134,"name":"free"}
{"timestamp":1738774194.1854289,"name":"clean"}
"""

TEST_EVENTLOG_OUTPUT = """\
1738774194.078433 submit userid=1001 urgency=16 flags=0 version=1
1738774194.094738 validate
1738774194.107318 depend
1738774194.107436 priority priority=16
1738774194.110116 alloc annotations={"sched":{"resource_summary":"rank[0-3]/core[0-7]"}}
1738774194.117796 start
1738774194.182067 finish status=0
1738774194.185372 release ranks="all" final=true
1738774194.185413 free
1738774194.185429 clean
"""

TEST_EVENTLOG_OUTPUT_HUMAN_UTC = """\
[Feb05 16:49] submit userid=1001 urgency=16 flags=0 version=1
[  +0.016305] validate
[  +0.028885] depend
[  +0.029003] priority priority=16
[  +0.031683] alloc annotations={"sched":{"resource_summary":"rank[0-3]/core[0-7]"}}
[  +0.039363] start
[  +0.103634] finish status=0
[  +0.106939] release ranks="all" final=true
[  +0.106980] free
[  +0.106996] clean
"""
TEST_EVENTLOG_OUTPUT_HUMAN_COLOR = """\
[1m[32m[Feb05 16:49][0m [33msubmit[0m [34muserid[0m=[37m1001[0m [34murgency[0m=[37m16[0m [34mflags[0m=[37m0[0m [34mversion[0m=[37m1[0m
[32m[  +0.016305][0m [33mvalidate[0m
[32m[  +0.028885][0m [33mdepend[0m
[32m[  +0.029003][0m [33mpriority[0m [34mpriority[0m=[37m16[0m
[32m[  +0.031683][0m [33malloc[0m [34mannotations[0m=[35m{"sched":{"resource_summary":"rank[0-3]/core[0-7]"}}[0m
[32m[  +0.039363][0m [33mstart[0m
[32m[  +0.103634][0m [33mfinish[0m [34mstatus[0m=[37m0[0m
[32m[  +0.106939][0m [33mrelease[0m [34mranks[0m=[35m"all"[0m [34mfinal[0m=[35mtrue[0m
[32m[  +0.106980][0m [33mfree[0m
[32m[  +0.106996][0m [33mclean[0m
"""

TEST_EVENTLOG_OUTPUT_OFFSET = """\
0.000000 submit userid=1001 urgency=16 flags=0 version=1
0.016305 validate
0.028885 depend
0.029003 priority priority=16
0.031683 alloc annotations={"sched":{"resource_summary":"rank[0-3]/core[0-7]"}}
0.039363 start
0.103634 finish status=0
0.106939 release ranks="all" final=true
0.106980 free
0.106996 clean
"""

TEST_EVENTLOG_OUTPUT_ISO_ZULU = """\
2025-02-05T16:49:54.078433Z submit userid=1001 urgency=16 flags=0 version=1
2025-02-05T16:49:54.094737Z validate
2025-02-05T16:49:54.107317Z depend
2025-02-05T16:49:54.107435Z priority priority=16
2025-02-05T16:49:54.110116Z alloc annotations={"sched":{"resource_summary":"rank[0-3]/core[0-7]"}}
2025-02-05T16:49:54.117795Z start
2025-02-05T16:49:54.182067Z finish status=0
2025-02-05T16:49:54.185371Z release ranks="all" final=true
2025-02-05T16:49:54.185413Z free
2025-02-05T16:49:54.185428Z clean
"""

test_eventlog_json = [x for x in TEST_EVENTLOG.splitlines()]
test_eventlog = [json.loads(x) for x in TEST_EVENTLOG.splitlines()]
test_eventlog_raw = [x for x in TEST_EVENTLOG_OUTPUT.splitlines()]
test_eventlog_human = [x for x in TEST_EVENTLOG_OUTPUT_HUMAN_UTC.splitlines()]
test_eventlog_human_color = [x for x in TEST_EVENTLOG_OUTPUT_HUMAN_COLOR.splitlines()]
test_eventlog_offset = [x for x in TEST_EVENTLOG_OUTPUT_OFFSET.splitlines()]
test_eventlog_iso_zulu = [x for x in TEST_EVENTLOG_OUTPUT_ISO_ZULU.splitlines()]


class TestEventLogFormatter(unittest.TestCase):

    def test_invalid_args(self):
        with self.assertRaises(ValueError):
            EventLogFormatter(format="foo")
        with self.assertRaises(ValueError):
            EventLogFormatter(timestamp_format="foo")
        with self.assertRaises(ValueError):
            EventLogFormatter(color="foo")

    def test_raw(self):
        evf = EventLogFormatter()
        for entry, expected in zip(test_eventlog, test_eventlog_raw):
            self.assertEqual(evf.format(entry).strip(), expected)

    def test_human(self):
        evf = EventLogFormatter(timestamp_format="human")
        for entry, expected in zip(test_eventlog, test_eventlog_human):
            self.assertEqual(evf.format(entry).strip(), expected)

    def test_offset(self):
        evf = EventLogFormatter(timestamp_format="offset")
        for entry, expected in zip(test_eventlog, test_eventlog_offset):
            self.assertEqual(evf.format(entry).strip(), expected)

    def test_iso(self):
        evf = EventLogFormatter(timestamp_format="iso")
        for entry, expected in zip(test_eventlog, test_eventlog_iso_zulu):
            self.assertEqual(evf.format(entry).strip(), expected)

    def test_json(self):
        evf = EventLogFormatter(format="json")
        for entry, expected in zip(test_eventlog, test_eventlog_json):
            self.assertEqual(evf.format(entry).strip(), expected)

    def test_color(self):
        evf = EventLogFormatter(timestamp_format="human", color="always")
        for entry, expected in zip(test_eventlog, test_eventlog_human_color):
            self.assertEqual(evf.format(entry).strip(), expected)


if __name__ == "__main__":
    os.environ["TZ"] = "UTC"
    time.tzset()
    unittest.main(testRunner=TAPTestRunner(), buffer=False)
