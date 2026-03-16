#!/usr/bin/env python3
###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import unittest

import subflux  # noqa: F401 - for PYTHONPATH
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.resource import ResourceSet
from pycotap import TAPTestRunner


class TestRSet(unittest.TestCase):
    R_input = """
    {
      "version": 1,
      "execution": {
        "R_lite": [
          {
            "rank": "0-3",
            "children": {
                "core": "0-3",
                "gpu": "0"
             }
          }
        ],
        "starttime": 0,
        "expiration": 0,
        "nodelist": [
          "fluke[0-3]"
        ]
      }
    }
    """
    R2 = """
    {
      "version": 1,
      "execution": {
        "R_lite": [
          {
            "rank": "10-13",
            "children": {
                "core": "0-3",
                "gpu": "0"
             }
          }
        ],
        "starttime": 0,
        "expiration": 0,
        "nodelist": [
          "fluke[10-13]"
        ]
      }
    }
    """
    # R with two ranks, each with 4 cores, and properties assigned across them.
    # rank 0: property "foo"; rank 1: properties "foo" and "bar"
    R_with_props = {
        "version": 1,
        "execution": {
            "R_lite": [{"rank": "0-1", "children": {"core": "0-3"}}],
            "starttime": 0,
            "expiration": 0,
            "nodelist": ["node[0-1]"],
            "properties": {"foo": "0-1", "bar": "1"},
        },
    }
    # A single-rank set with property "baz" on rank 2, used to merge into R_with_props
    R_rank2_with_baz = {
        "version": 1,
        "execution": {
            "R_lite": [{"rank": "2", "children": {"core": "0-3"}}],
            "starttime": 0,
            "expiration": 0,
            "nodelist": ["node2"],
            "properties": {"baz": "2"},
        },
    }

    # Small R dicts used in place of Rlist()-based construction
    R_rank0_2c_1g = {
        "version": 1,
        "execution": {
            "R_lite": [{"rank": "0", "children": {"core": "0-1", "gpu": "0"}}],
            "starttime": 0,
            "expiration": 0,
            "nodelist": ["node0"],
        },
    }
    R_rank4_4c_1g = {
        "version": 1,
        "execution": {
            "R_lite": [{"rank": "4", "children": {"core": "0-3", "gpu": "0"}}],
            "starttime": 0,
            "expiration": 0,
        },
    }
    R_rank5_2c = {
        "version": 1,
        "execution": {
            "R_lite": [{"rank": "5", "children": {"core": "0-1"}}],
            "starttime": 0,
            "expiration": 0,
        },
    }

    def test_init_string(self):
        #  init by string
        rset = ResourceSet(self.R_input)
        self.assertEqual(str(rset), "rank[0-3]/core[0-3],gpu0")
        self.assertEqual(rset.ncores, 16)
        self.assertEqual(rset.ngpus, 4)

        rset = ResourceSet(self.R2)
        self.assertEqual(str(rset), "rank[10-13]/core[0-3],gpu0")
        self.assertEqual(rset.ncores, 16)
        self.assertEqual(rset.ngpus, 4)

    def test_init_dict(self):
        #  init by dict
        rdict = json.loads(self.R_input)
        rset = ResourceSet(rdict)
        self.assertEqual(str(rset), "rank[0-3]/core[0-3],gpu0")
        self.assertEqual(rset.ncores, 16)
        self.assertEqual(rset.ngpus, 4)
        self.assertEqual(rset.nnodes, 4)

        rdict = json.loads(self.R2)
        rset = ResourceSet(rdict)
        self.assertEqual(str(rset), "rank[10-13]/core[0-3],gpu0")
        self.assertEqual(rset.ncores, 16)
        self.assertEqual(rset.ngpus, 4)
        self.assertEqual(rset.nnodes, 4)

    def test_init_implementation(self):
        #  init by R JSON dict (rank 0, 2 cores, 1 gpu)
        rset = ResourceSet(self.R_rank0_2c_1g)
        self.assertEqual(str(rset), "rank0/core[0-1],gpu0")
        self.assertEqual(rset.ncores, 2)
        self.assertEqual(rset.ngpus, 1)
        self.assertEqual(rset.nnodes, 1)

    def test_init_empty(self):
        rset = ResourceSet()
        self.assertEqual(str(rset), "")
        self.assertEqual(rset.nnodes, 0)
        self.assertEqual(rset.ncores, 0)
        self.assertEqual(rset.ngpus, 0)

    def test_init_assertions(self):
        #  test invalid implementation
        with self.assertRaises(TypeError):
            ResourceSet((1, 2))

        #  test invalid resource set string:
        with self.assertRaises(ValueError):
            ResourceSet("")

        #  test invalid version
        with self.assertRaises(ValueError):
            ResourceSet({"version": 199})

        #  test string with invalid version
        with self.assertRaises(ValueError):
            arg = json.dumps({"version": 199})
            ResourceSet(arg)

        #  test dict without 'version' string
        with self.assertRaises(KeyError):
            ResourceSet({})

        #  test invalid JSON string
        with self.assertRaises(json.decoder.JSONDecodeError):
            ResourceSet("{")

    def test_op(self):
        set1 = ResourceSet(self.R_input)
        set2 = set1.copy().remove_ranks("2-3")

        # difference
        result = set1 - set2
        self.assertIsInstance(result, ResourceSet)
        self.assertEqual(str(result), "rank[2-3]/core[0-3],gpu0")

        set3 = result
        # union
        result = set3 | set1
        self.assertIsInstance(result, ResourceSet)
        self.assertEqual(str(result), "rank[0-3]/core[0-3],gpu0")

        # intersection
        result = set1 & set2
        self.assertIsInstance(result, ResourceSet)
        self.assertEqual(str(result), "rank[0-1]/core[0-3],gpu0")

    def test_encode(self):
        rset = ResourceSet(self.R_input)
        rstring = rset.encode()
        rset2 = ResourceSet(rstring)
        self.assertEqual(str(rset), str(rset2))

        rset = ResourceSet(self.R2)
        rstring = rset.encode()
        rset2 = ResourceSet(rstring)
        self.assertEqual(str(rset), str(rset2))

    def test_append(self):
        rset = ResourceSet(self.R_input)
        rset2 = ResourceSet(self.R_rank4_4c_1g)
        rset.append(rset2)
        self.assertEqual(str(rset.ranks), "0-4")
        self.assertEqual(rset.ncores, 20)
        self.assertEqual(rset.ngpus, 5)

    def test_add(self):
        rset = ResourceSet(self.R_input)
        rset2 = ResourceSet(self.R_rank4_4c_1g)
        rset.add(rset2)
        self.assertEqual(str(rset.ranks), "0-4")
        self.assertEqual(rset.ncores, 20)
        self.assertEqual(rset.ngpus, 5)
        # adding same resources is idempotent
        rset2 = ResourceSet(self.R_rank4_4c_1g)
        rset.add(rset2)
        self.assertEqual(str(rset.ranks), "0-4")
        self.assertEqual(rset.ncores, 20)
        self.assertEqual(rset.ngpus, 5)

    def test_nodelist(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset.nodelist, Hostlist)
        self.assertEqual(rset.nodelist.count(), 4)
        self.assertEqual(str(rset.nodelist), "fluke[0-3]")

        rset = ResourceSet(self.R2)
        self.assertIsInstance(rset.nodelist, Hostlist)
        self.assertEqual(rset.nodelist.count(), 4)
        self.assertEqual(str(rset.nodelist), "fluke[10-13]")

    def test_ranks(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset.ranks, IDset)
        self.assertEqual(rset.ranks.count(), 4)
        self.assertEqual(str(rset.ranks), "0-3")

        rset = ResourceSet(self.R2)
        self.assertIsInstance(rset.ranks, IDset)
        self.assertEqual(rset.ranks.count(), 4)
        self.assertEqual(str(rset.ranks), "10-13")

    def test_state(self):
        rset = ResourceSet(self.R_input)
        self.assertIsNone(rset.state)
        rset.state = "up"
        self.assertEqual(rset.state, "up")

    def test_host_ranks(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset, ResourceSet)
        self.assertEqual(rset.host_ranks("fluke0"), [0])
        self.assertEqual(rset.host_ranks("fluke1"), [1])
        self.assertEqual(rset.host_ranks("fluke2"), [2])
        self.assertEqual(rset.host_ranks("fluke3"), [3])
        self.assertEqual(rset.host_ranks("fluke[0,3]"), [0, 3])
        self.assertEqual(rset.host_ranks("fluke[0-2]"), [0, 1, 2])
        self.assertEqual(
            rset.host_ranks("fluke[0-2,7]", ignore_nomatch=True), [0, 1, 2]
        )
        with self.assertRaises(FileNotFoundError):
            rset.host_ranks("fluke7")

        rset = ResourceSet(self.R2)
        self.assertIsInstance(rset, ResourceSet)
        self.assertEqual(rset.host_ranks("fluke10"), [10])
        self.assertEqual(rset.host_ranks("fluke11"), [11])
        self.assertEqual(rset.host_ranks("fluke12"), [12])
        self.assertEqual(rset.host_ranks("fluke13"), [13])
        self.assertEqual(rset.host_ranks("fluke[10,13]"), [10, 13])
        self.assertEqual(rset.host_ranks("fluke[10-12]"), [10, 11, 12])
        self.assertEqual(
            rset.host_ranks("fluke[0-2,10-12]", ignore_nomatch=True), [10, 11, 12]
        )
        with self.assertRaises(FileNotFoundError):
            rset.host_ranks("fluke[0-3,10-12]")

    def test_properties(self):
        rset = ResourceSet(self.R_input)
        self.assertIsInstance(rset, ResourceSet)

        rset.set_property("xx", "0-1")
        rset.set_property("zz", "2-3")
        rset.set_property("aa", "1")

        with self.assertRaises(ValueError):
            rset.set_property("x^y")
        with self.assertRaises(ValueError):
            rset.set_property("yy", "0-6")
        with self.assertRaises(ValueError):
            rset.set_property("yy", "foo")

        # copy_constraint() with invalid property returns empty set
        empty = rset.copy_constraint({"properties": ["foo"]})
        self.assertIsInstance(empty, ResourceSet)
        self.assertEqual(str(empty), "")

        xx = rset.copy_constraint({"properties": ["xx"]})
        self.assertIsInstance(xx, ResourceSet)
        self.assertEqual(str(xx), "rank[0-1]/core[0-3],gpu0")

        zz = rset.copy_constraint({"properties": ["zz"]})
        self.assertIsInstance(zz, ResourceSet)
        self.assertEqual(str(zz), "rank[2-3]/core[0-3],gpu0")

        aa = rset.copy_constraint({"properties": ["aa"]})
        self.assertIsInstance(aa, ResourceSet)
        self.assertEqual(str(aa), "rank1/core[0-3],gpu0")

        aa = xx.copy_constraint({"properties": ["aa"]})
        self.assertIsInstance(aa, ResourceSet)
        self.assertEqual(str(aa), "rank1/core[0-3],gpu0")

        #  not (xx and aa)
        notax = rset.copy_constraint({"not": [{"properties": ["aa", "xx"]}]})
        self.assertIsInstance(notax, ResourceSet)
        self.assertEqual(str(notax), "rank[0,2-3]/core[0-3],gpu0")

        #  not xx and not aa
        notax2 = rset.copy_constraint({"properties": ["^aa", "^xx"]})
        self.assertIsInstance(notax2, ResourceSet)
        self.assertEqual(str(notax2), "rank[2-3]/core[0-3],gpu0")

        with self.assertRaises(ValueError):
            rset.copy_constraint({"foo": []})
        with self.assertRaises(ValueError):
            rset.copy_constraint({"properties": [42]})

    def test_count(self):
        rset = ResourceSet(self.R_input)
        self.assertEqual(rset.count("core"), 16)
        self.assertEqual(rset.count("gpu"), 4)
        # unknown resource type returns 0
        self.assertEqual(rset.count("notaresource"), 0)

    def test_rlist(self):
        rset = ResourceSet(self.R_input)
        self.assertEqual(rset.rlist, str(rset))
        self.assertEqual(rset.rlist, "rank[0-3]/core[0-3],gpu0")

    def test_copy(self):
        rset = ResourceSet(self.R_input)
        rset.state = "up"
        cp = rset.copy()
        self.assertIsInstance(cp, ResourceSet)
        self.assertEqual(str(cp), str(rset))
        self.assertEqual(cp.state, "up")
        # Verify it's a real copy (modifications don't affect original)
        cp.remove_ranks("0")
        self.assertEqual(str(rset), "rank[0-3]/core[0-3],gpu0")

    def test_copy_ranks(self):
        rset = ResourceSet(self.R_input)
        rset.state = "up"

        # copy single rank
        cp = rset.copy_ranks("0")
        self.assertIsInstance(cp, ResourceSet)
        self.assertEqual(str(cp), "rank0/core[0-3],gpu0")
        self.assertEqual(cp.state, "up")

        # copy multiple ranks as string
        cp = rset.copy_ranks("1-2")
        self.assertEqual(str(cp), "rank[1-2]/core[0-3],gpu0")

        # copy via IDset
        cp = rset.copy_ranks(IDset("0,3"))
        self.assertEqual(str(cp), "rank[0,3]/core[0-3],gpu0")

    def test_remove_ranks(self):
        rset = ResourceSet(self.R_input)
        rset.remove_ranks("0-1")
        self.assertEqual(str(rset), "rank[2-3]/core[0-3],gpu0")
        self.assertEqual(rset.nnodes, 2)

        # remove via IDset
        rset = ResourceSet(self.R_input)
        rset.remove_ranks(IDset("0,3"))
        self.assertEqual(str(rset), "rank[1-2]/core[0-3],gpu0")

    def test_get_properties(self):
        rset = ResourceSet(self.R_input)
        rset.set_property("foo", "0-1")
        rset.set_property("bar", "2-3")
        props = json.loads(rset.get_properties())
        self.assertIsInstance(props, dict)
        self.assertIn("foo", props)
        self.assertIn("bar", props)

    def test_properties_attribute(self):
        rset = ResourceSet(self.R_input)
        # no properties set
        self.assertEqual(rset.properties, "")

        rset.set_property("foo", "0-1")
        rset.set_property("bar", "2-3")
        prop_list = rset.properties.split(",")
        self.assertIn("foo", prop_list)
        self.assertIn("bar", prop_list)

    def test_set_property_all_ranks(self):
        rset = ResourceSet(self.R_input)
        # set property on all ranks (no ranks arg)
        rset.set_property("all")
        result = rset.copy_constraint({"properties": ["all"]})
        self.assertEqual(str(result), str(rset))

    def test_expiration(self):
        rset = ResourceSet(self.R_input)
        # default expiration from R_input is 0
        self.assertEqual(rset.expiration, 0.0)

        rset.expiration = 12345.0
        self.assertEqual(rset.expiration, 12345.0)

        # round-trip through encode/decode preserves expiration
        rset2 = ResourceSet(rset.encode())
        self.assertEqual(rset2.expiration, 12345.0)

    def test_starttime(self):
        rset = ResourceSet(self.R_input)
        # default starttime from R_input is 0
        self.assertEqual(rset.starttime, 0.0)

        rset.starttime = 9999.0
        self.assertEqual(rset.starttime, 9999.0)

        # round-trip through encode/decode preserves starttime
        rset2 = ResourceSet(rset.encode())
        self.assertEqual(rset2.starttime, 9999.0)

    def test_starttime_expiration_together(self):
        rset = ResourceSet(self.R_input)
        rset.starttime = 1000.0
        rset.expiration = 2000.0
        self.assertEqual(rset.starttime, 1000.0)
        self.assertEqual(rset.expiration, 2000.0)

        rset2 = ResourceSet(rset.encode())
        self.assertEqual(rset2.starttime, 1000.0)
        self.assertEqual(rset2.expiration, 2000.0)

    def test_multi_arg_ops(self):
        rset = ResourceSet(self.R_input)  # ranks 0-3
        r2 = ResourceSet(self.R2)  # ranks 10-13
        extra = ResourceSet(self.R_rank5_2c)

        # multi-arg union
        result = rset.union(r2, extra)
        self.assertIn("5", str(result.ranks))
        self.assertIn("10", str(result.ranks))
        self.assertIn("0", str(result.ranks))

        # multi-arg diff
        rset_copy = ResourceSet(self.R_input)
        r_01 = rset_copy.copy_ranks("0-1")
        r_23 = rset_copy.copy_ranks("2-3")
        result = rset_copy.diff(r_01, r_23)
        self.assertEqual(str(result), "")

        # multi-arg intersect
        rset_copy = ResourceSet(self.R_input)
        r_0_2 = rset_copy.copy_ranks("0-2")
        r_1_3 = rset_copy.copy_ranks("1-3")
        result = rset_copy.intersect(r_0_2, r_1_3)
        self.assertEqual(str(result.ranks), "1-2")

    def test_add_propagates_properties(self):
        """add() must carry properties from the new ranks into the merged set."""
        rset = ResourceSet(self.R_with_props)  # ranks 0-1, props foo/bar
        rset2 = ResourceSet(self.R_rank2_with_baz)  # rank 2, prop baz
        rset.add(rset2)
        props = json.loads(rset.get_properties())
        self.assertIn("foo", props)
        self.assertIn("bar", props)
        self.assertIn("baz", props)
        # baz should cover only rank 2
        self.assertEqual(props["baz"], "2")

    def test_append_propagates_properties(self):
        """append() must carry properties from the appended set into self."""
        rset = ResourceSet(self.R_with_props)  # ranks 0-1
        rset2 = ResourceSet(self.R_rank2_with_baz)  # rank 2
        rset.append(rset2)
        props = json.loads(rset.get_properties())
        self.assertIn("foo", props)
        self.assertIn("baz", props)

    def test_add_propagates_nodelist(self):
        """add() of a set that has a nodelist must preserve nodelist in encode()."""
        rset = ResourceSet(self.R_with_props)  # has nodelist
        rset2 = ResourceSet(self.R_rank2_with_baz)  # has nodelist
        rset.add(rset2)
        encoded = json.loads(rset.encode())
        nodelist = encoded["execution"].get("nodelist", [])
        self.assertTrue(nodelist, "nodelist should be present after add()")
        # All three hostnames should appear
        hl = Hostlist(",".join(nodelist))
        self.assertIn("node0", list(hl))
        self.assertIn("node2", list(hl))

    def test_diff_partial_rank(self):
        """diff() must do core-level subtraction, not whole-rank removal."""
        # rank 0 with 4 cores; subtract 1 core → 3 cores remain on rank 0
        rset = ResourceSet(self.R_with_props)  # ranks 0-1, 4 cores each
        one_core = ResourceSet(
            {
                "version": 1,
                "execution": {
                    "R_lite": [{"rank": "0", "children": {"core": "0"}}],
                    "starttime": 0,
                    "expiration": 0,
                },
            }
        )
        result = rset - one_core
        # rank 0 must still be present with 3 cores; rank 1 untouched
        self.assertIn("0", str(result.ranks))
        self.assertIn("1", str(result.ranks))
        self.assertEqual(result.ncores, 7)  # 3 + 4

    def test_diff_preserves_properties(self):
        """diff() must restrict properties to surviving ranks."""
        rset = ResourceSet(self.R_with_props)  # ranks 0-1; foo on 0-1, bar on 1
        rank1_only = ResourceSet(
            {
                "version": 1,
                "execution": {
                    "R_lite": [{"rank": "1", "children": {"core": "0-3"}}],
                    "starttime": 0,
                    "expiration": 0,
                },
            }
        )
        result = rset - rank1_only
        # Only rank 0 survives; foo should remain, bar should be gone
        self.assertEqual(str(result.ranks), "0")
        props = json.loads(result.get_properties())
        self.assertIn("foo", props)
        self.assertNotIn("bar", props)

    def test_encode_round_trips_properties(self):
        """Properties must survive encode() → ResourceSet() round-trip."""
        rset = ResourceSet(self.R_with_props)
        rset2 = ResourceSet(rset.encode())
        props = json.loads(rset2.get_properties())
        self.assertIn("foo", props)
        self.assertIn("bar", props)
        self.assertEqual(props["foo"], "0-1")
        self.assertEqual(props["bar"], "1")

    def test_encode_round_trips_nodelist(self):
        """Nodelist must survive encode() → ResourceSet() round-trip."""
        rset = ResourceSet(self.R_with_props)
        rset2 = ResourceSet(rset.encode())
        self.assertEqual(str(rset2.nodelist), "node[0-1]")

    def test_append_merges_disjoint_cores_same_rank(self):
        """append() of disjoint core IDs on the same rank must merge, not overwrite.

        Mirrors C rlist_append / rnode_add behaviour: appending core[4-7] to
        a rank that already has core[0-3] should produce core[0-7], not core[4-7].
        """
        r_first = {
            "version": 1,
            "execution": {
                "R_lite": [{"rank": "0", "children": {"core": "0-3"}}],
                "starttime": 0,
                "expiration": 0,
            },
        }
        r_second = {
            "version": 1,
            "execution": {
                "R_lite": [{"rank": "0", "children": {"core": "4-7"}}],
                "starttime": 0,
                "expiration": 0,
            },
        }
        rset = ResourceSet(r_first)
        rset.append(ResourceSet(r_second))
        self.assertEqual(rset.ncores, 8)
        self.assertEqual(str(rset), "rank0/core[0-7]")

    def test_union_merges_resources_for_shared_rank(self):
        """union() of sets with overlapping ranks must merge resource IDs.

        Mirrors C rlist_union / rnode_union behaviour: union of rank0/core[0-3]
        and rank0/gpu0 should produce rank0/core[0-3],gpu0, not just rank0/core[0-3].
        """
        r_cores = {
            "version": 1,
            "execution": {
                "R_lite": [{"rank": "0", "children": {"core": "0-3"}}],
                "starttime": 0,
                "expiration": 0,
            },
        }
        r_gpus = {
            "version": 1,
            "execution": {
                "R_lite": [{"rank": "0", "children": {"gpu": "0"}}],
                "starttime": 0,
                "expiration": 0,
            },
        }
        result = ResourceSet(r_cores) | ResourceSet(r_gpus)
        self.assertEqual(result.ncores, 4)
        self.assertEqual(result.ngpus, 1)
        self.assertEqual(str(result), "rank0/core[0-3],gpu0")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())


# vi: ts=4 sw=4 expandtab
