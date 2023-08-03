#!/usr/bin/env python3

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import ast
import errno
import unittest

import flux
import flux.constants
import flux.kvs
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestKVS(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.f = flux.Flux()

    def test_01_kvs_dir_open(self):
        with flux.kvs.get_dir(self.f) as d:
            self.assertIsNotNone(d)

    def set_and_check_context(self, key, value, type):
        kd = flux.kvs.KVSDir(self.f)
        kd[key] = value
        kd.commit()

        kd2 = flux.kvs.KVSDir(self.f)
        nv = kd2[key]
        if isinstance(value, bytes) and type is str:
            self.assertEqual(value.decode("utf-8"), nv)
        elif isinstance(value, bytes) and type is dict:
            # convert value bytes into string, then convert into
            # Python dict via ast.literal_evl for comparison.
            self.assertDictEqual(ast.literal_eval(value.decode("utf-8")), nv)
        else:
            self.assertEqual(value, nv)
        if type is not None:
            self.assertTrue(isinstance(nv, type))

        return kd2

    def test_02_set_int(self):
        self.set_and_check_context("int", 10, int)

    def test_03_set_float(self):
        self.set_and_check_context("float", 10.5, float)

    def test_04_set_string(self):
        self.set_and_check_context("string", "stuff", str)

    def test_05_set_none(self):
        self.set_and_check_context("none", None, None)

    def test_06_set_unicode(self):
        self.set_and_check_context("unicode", "\u32db \u263a \u32e1", str)

    def test_07_set_bytes(self):
        self.set_and_check_context("bytes", bytes.fromhex("deadbeef"), bytes)

    def test_08_set_list(self):
        self.set_and_check_context("list", [1, 2, 3, 4], list)

    def test_09_set_dict(self):
        self.set_and_check_context(
            "dict", {"thing": "stuff", "other thing": "more stuff"}, dict
        )

    def test_10_set_legal_json(self):
        self.set_and_check_context("badjson", b"{}", dict)

    def test_11_set_not_legal_json(self):
        self.set_and_check_context("badjson", b"{", str)

    def test_12_exists_dir(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("pytestdir")
        self.assertTrue(flux.kvs.exists(self.f, "pytestdir"))

    def test_13_exists_true(self):
        flux.kvs.put(self.f, "thing", 15)
        flux.kvs.commit(self.f)
        self.assertTrue(flux.kvs.exists(self.f, "thing"))

    def test_14_exists_false(self):
        self.assertFalse(flux.kvs.exists(self.f, "argbah"))

    def test_15_isdir_true(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("testisdir")
        self.assertTrue(flux.kvs.isdir(self.f, "testisdir"))

    def test_16_isdir_false(self):
        flux.kvs.put(self.f, "testisdirkey", 15)
        flux.kvs.commit(self.f)
        self.assertFalse(flux.kvs.isdir(self.f, "testisdirkey"))
        self.assertFalse(flux.kvs.isdir(self.f, "not_a_key_i_made"))

    def test_17_commit_flags(self):
        flux.kvs.put(self.f, "flagcheck", 42)
        flux.kvs.commit(self.f, 1)
        self.assertTrue(flux.kvs.exists(self.f, "flagcheck"))

    def test_18_remove(self):
        kd = self.set_and_check_context("todel", "things to delete", str)
        del kd["todel"]
        kd.commit()
        with self.assertRaises(KeyError):
            stuff = kd["todel"]
            print(stuff)

    def test_19_fill(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.fill({"things": 1, "stuff": "strstuff", "dir.other_thing": "dirstuff"})
            kd.commit()

        with flux.kvs.get_dir(self.f) as kd2:
            self.assertEqual(kd2["things"], 1)
            self.assertEqual(kd2["stuff"], "strstuff")
            self.assertEqual(kd2["dir"]["other_thing"], "dirstuff")

    def test_20_mkdir_fill(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir(
                "mkdirfill",
                {
                    "thingies": 1,
                    "stuffs": "strstuffs",
                    "dir.other_thingies": "dirstuffs",
                },
            )
            kd.commit()

        with flux.kvs.get_dir(self.f) as kd2:
            self.assertEqual(kd2["mkdirfill.thingies"], 1)
            self.assertEqual(kd2["mkdirfill.stuffs"], "strstuffs")
            self.assertEqual(kd2["mkdirfill"]["dir"]["other_thingies"], "dirstuffs")

    def test_21_set_deep(self):
        self.set_and_check_context("a.b.c.e.f.j.k", 5, int)

    def test_22_bad_init(self):
        with self.assertRaises(ValueError):
            flux.kvs.KVSDir()

    def test_23_key_at(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("testkeyat")
        with flux.kvs.get_dir(self.f, "testkeyat") as kd:
            self.assertEqual(kd.key_at("meh"), "testkeyat.meh")

    def test_24_exists_initial_path(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd["exists1"] = 1
            kd.mkdir("existssubdir")
            kd["existssubdir.exists2"] = 2

        with flux.kvs.get_dir(self.f) as kd2:
            self.assertTrue(kd2.exists("exists1"))
            self.assertTrue(kd2.exists("existssubdir.exists2"))

        with flux.kvs.get_dir(self.f, "existssubdir") as kd3:
            self.assertFalse(kd3.exists("exists1"))
            self.assertTrue(kd3.exists("exists2"))

    def test_25_key_initial_path(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("initialpath")

        kd2 = flux.kvs.KVSDir(self.f)
        kd2["initialpath.a"] = 1
        kd2["initialpath"]["b"] = 2
        kd2.commit()

        kd3 = flux.kvs.KVSDir(self.f, "initialpath")
        kd3["c"] = 3
        kd3["d.e.f"] = 4
        kd3.commit()

        kd4 = flux.kvs.KVSDir(self.f)
        self.assertEqual(kd4["initialpath.a"], 1)
        self.assertEqual(kd4["initialpath"]["a"], 1)
        self.assertEqual(kd4["initialpath.b"], 2)
        self.assertEqual(kd4["initialpath"]["b"], 2)
        self.assertEqual(kd4["initialpath.c"], 3)
        self.assertEqual(kd4["initialpath"]["c"], 3)
        self.assertEqual(kd4["initialpath.d.e.f"], 4)
        self.assertEqual(kd4["initialpath"]["d.e.f"], 4)
        self.assertEqual(kd4["initialpath"]["d"]["e"]["f"], 4)

        kd5 = flux.kvs.KVSDir(self.f, "initialpath")
        self.assertEqual(kd5["a"], 1)
        self.assertEqual(kd5["b"], 2)
        self.assertEqual(kd5["c"], 3)
        self.assertEqual(kd5["d.e.f"], 4)
        self.assertEqual(kd5["d"]["e"]["f"], 4)

    def test_26_fill_initial_path(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("fillinitialpath")

        with flux.kvs.get_dir(self.f, "fillinitialpath") as kd2:
            kd2.fill({"g": 1, "h": "bar", "i.j.k": "baz"})
            kd2.commit()

        with flux.kvs.get_dir(self.f) as kd3:
            self.assertEqual(kd3["fillinitialpath.g"], 1)
            self.assertEqual(kd3["fillinitialpath.h"], "bar")
            self.assertEqual(kd3["fillinitialpath.i.j.k"], "baz")
            self.assertEqual(kd3["fillinitialpath.i"]["j"]["k"], "baz")

        with flux.kvs.get_dir(self.f, "fillinitialpath") as kd4:
            self.assertEqual(kd4["g"], 1)
            self.assertEqual(kd4["h"], "bar")
            self.assertEqual(kd4["i.j.k"], "baz")
            self.assertEqual(kd4["i"]["j"]["k"], "baz")

    def test_27_mkdir_initial_path(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("mkdirinitialpath", {"l": 1, "m": "bar", "n.o.p": "baz"})
            kd.commit()

        with flux.kvs.get_dir(self.f) as kd2:
            self.assertEqual(kd2["mkdirinitialpath.l"], 1)
            self.assertEqual(kd2["mkdirinitialpath.m"], "bar")
            self.assertEqual(kd2["mkdirinitialpath.n.o.p"], "baz")
            self.assertEqual(kd2["mkdirinitialpath.n"]["o"]["p"], "baz")

        with flux.kvs.get_dir(self.f, "mkdirinitialpath") as kd3:
            self.assertEqual(kd3["l"], 1)
            self.assertEqual(kd3["m"], "bar")
            self.assertEqual(kd3["n.o.p"], "baz")
            self.assertEqual(kd3["n"]["o"]["p"], "baz")

    def test_28_files(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("filestest", {"somefile": 1, "somefile2": 2})
            kd.mkdir("filestest.subdir")
            kd.commit()

        with flux.kvs.get_dir(self.f, "filestest") as kd2:
            files = [x for x in kd2.files()]
            self.assertEqual(len(files), 2)
            self.assertIn("somefile", files)
            self.assertIn("somefile2", files)

    def test_29_directories(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("directoriestest", {"somefile": 1, "somefile2": 2})
            kd.mkdir("directoriestest.subdir")
            kd.commit()

        with flux.kvs.get_dir(self.f, "directoriestest") as kd2:
            directories = [x for x in kd2.directories()]
            self.assertEqual(len(directories), 1)
            self.assertIn("subdir", directories)

    def test_30_list_all(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("listalltest", {"somefile": 1, "somefile2": 2})
            kd.mkdir("listalltest.subdir")
            kd.commit()

        with flux.kvs.get_dir(self.f, "listalltest") as kd2:
            (files, directories) = kd2.list_all()
            self.assertEqual(len(files), 2)
            self.assertEqual(len(directories), 1)
            self.assertIn("somefile", files)
            self.assertIn("somefile2", files)
            self.assertIn("subdir", directories)

    def test_31_read_non_existent(self):
        with self.assertRaises(KeyError):
            print(
                flux.kvs.KVSDir(self.f)[
                    "crazykeythatclearlydoesntexistandneverwillinanyuniverse"
                ]
            )

    def test_32_read_non_existent_basedir(self):
        with self.assertRaisesRegex(EnvironmentError, "No such file"):
            print(
                flux.kvs.KVSDir(
                    self.f, "crazykeythatclearlydoesntexistandneverwillinanyuniverse"
                )
            )

    def test_33_iterator(self):
        keys = ["testdir1a." + str(x) for x in range(1, 15)]
        with flux.kvs.get_dir(self.f) as kd:
            for k in keys:
                kd[k] = "bar"

        with flux.kvs.get_dir(self.f, "testdir1a") as kd:
            print(kd.keys())
            for k, v in kd.items():
                self.assertEqual(v, "bar")
                print("passed {}".format(k))

    def test_34_walk(self):
        keys = ["testwalk." + str(x) for x in range(1, 15)]
        with flux.kvs.get_dir(self.f) as kd:
            for k in keys:
                kd[k] = "bar"
                kd[k + "d." + k] = "meh"
        walk_gen = flux.kvs.walk("testwalk", flux_handle=self.f, topdown=True)
        (r, ds, fs) = next(walk_gen)
        print(r, ds, fs)
        self.assertEqual(r, "")
        self.assertEqual(len(list(ds)), 14)
        self.assertEqual(len(list(fs)), 14)

        for r, ds, fs in walk_gen:
            pass

    def test_35_walk_with_no_handle(self):
        with self.assertRaises(ValueError):
            flux.kvs.walk("dir").next()

    def test_36_put_mkdir(self):
        flux.kvs.put_mkdir(self.f, "txn_mkdir")
        flux.kvs.commit(self.f)
        self.assertTrue(flux.kvs.exists(self.f, "txn_mkdir"))

    def test_37_put_unlink(self):
        flux.kvs.put(self.f, "txn_unlink", 1)
        flux.kvs.commit(self.f)
        flux.kvs.put_unlink(self.f, "txn_unlink")
        flux.kvs.commit(self.f)
        self.assertFalse(flux.kvs.exists(self.f, "txn_unlink"))

    def test_38_put_symlink(self):
        flux.kvs.put_symlink(self.f, "txn_symlink", "txn_target")
        flux.kvs.commit(self.f)
        self.assertFalse(flux.kvs.exists(self.f, "txn_symlink"))

    def bad_input(self, func, *args):
        with self.assertRaises(OSError) as ctx:
            func(*args)
        self.assertEqual(ctx.exception.errno, errno.EINVAL)

    def test_39_exists_bad_input(self):
        self.bad_input(flux.kvs.exists, self.f, "")

    def test_40_isdir_bad_input(self):
        self.bad_input(flux.kvs.isdir, self.f, "")

    def test_41_get_bad_input(self):
        self.bad_input(flux.kvs.get, self.f, "")

    def test_42_get_dir_bad_input(self):
        self.bad_input(flux.kvs.get_dir, self.f, "")

    def test_43_put_exception_bad_input(self):
        self.bad_input(flux.kvs.put, self.f, "", "")

    def test_44_put_mkdir_exception_bad_input(self):
        self.bad_input(flux.kvs.put_mkdir, self.f, "")

    def test_45_put_unlink_bad_input(self):
        self.bad_input(flux.kvs.put_unlink, self.f, "")

    def test_46_put_symlink_bad_input(self):
        self.bad_input(flux.kvs.put_symlink, self.f, "", "")

    # try to overwrite root dir, will fail on commit
    def test_47_commit_fail(self):
        with self.assertRaises(OSError) as ctx:
            flux.kvs.put(self.f, ".", "foof")
            flux.kvs.commit(self.f)
        self.assertEqual(ctx.exception.errno, errno.EINVAL)

        # Issue #5333, make sure internal bad transaction cleared and
        # subsequent commit works
        flux.kvs.commit(self.f)

    # just testing that passing flags work, these are pitiful KVS
    # changes and the flags don't mean much
    def test_48_commit_flags(self):
        flux.kvs.put(self.f, "commitflags", "foo")
        flux.kvs.commit(self.f, flux.constants.FLUX_KVS_NO_MERGE)
        flux.kvs.put(self.f, "commitflags", "baz")
        flux.kvs.commit(self.f, flux.constants.FLUX_KVS_TXN_COMPACT)
        flux.kvs.put(self.f, "commitflags", "bar")
        flux.kvs.commit(self.f, flux.constants.FLUX_KVS_SYNC)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
