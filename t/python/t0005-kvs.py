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

    def test_basic_01_kvs_dir_open(self):
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

    def test_basic_02_set_int(self):
        self.set_and_check_context("int", 10, int)

    def test_basic_03_set_float(self):
        self.set_and_check_context("float", 10.5, float)

    def test_basic_04_set_string(self):
        self.set_and_check_context("string", "stuff", str)

    def test_basic_05_set_none(self):
        self.set_and_check_context("none", None, None)

    def test_basic_06_set_unicode(self):
        self.set_and_check_context("unicode", "\u32db \u263a \u32e1", str)

    def test_basic_07_set_bytes(self):
        self.set_and_check_context("bytes", bytes.fromhex("deadbeef"), bytes)

    def test_basic_08_set_list(self):
        self.set_and_check_context("list", [1, 2, 3, 4], list)

    def test_basic_09_set_dict(self):
        self.set_and_check_context(
            "dict", {"thing": "stuff", "other thing": "more stuff"}, dict
        )

    def test_basic_10_set_legal_json(self):
        self.set_and_check_context("badjson", b"{}", dict)

    def test_basic_11_set_not_legal_json(self):
        self.set_and_check_context("badjson", b"{", str)

    def test_basic_12_set_deep(self):
        self.set_and_check_context("a.b.c.e.f.j.k", 5, int)

    def test_api_01_exists_dir(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("pytestdir")
        self.assertTrue(flux.kvs.exists(self.f, "pytestdir"))

    def test_api_02_exists_true(self):
        flux.kvs.put(self.f, "thing", 15)
        flux.kvs.commit(self.f)
        self.assertTrue(flux.kvs.exists(self.f, "thing"))

    def test_api_03_exists_false(self):
        self.assertFalse(flux.kvs.exists(self.f, "argbah"))

    def test_api_04_isdir_true(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("testisdir")
        self.assertTrue(flux.kvs.isdir(self.f, "testisdir"))

    def test_api_05_isdir_false(self):
        flux.kvs.put(self.f, "testisdirkey", 15)
        flux.kvs.commit(self.f)
        self.assertFalse(flux.kvs.isdir(self.f, "testisdirkey"))
        self.assertFalse(flux.kvs.isdir(self.f, "not_a_key_i_made"))

    def test_api_06_put_raw_bytes(self):
        flux.kvs.put(self.f, "txn_raw_bytes", "foobar".encode("utf-8"), raw=True)
        flux.kvs.commit(self.f)
        self.assertTrue(flux.kvs.exists(self.f, "txn_raw_bytes"))

    def test_api_07_put_raw_string(self):
        flux.kvs.put(self.f, "txn_raw_string", "foobar", raw=True)
        flux.kvs.commit(self.f)
        self.assertTrue(flux.kvs.exists(self.f, "txn_raw_string"))

    def test_api_08_put_mkdir(self):
        flux.kvs.put_mkdir(self.f, "txn_mkdir")
        flux.kvs.commit(self.f)
        self.assertTrue(flux.kvs.exists(self.f, "txn_mkdir"))

    def test_api_09_put_unlink(self):
        flux.kvs.put(self.f, "txn_unlink", 1)
        flux.kvs.commit(self.f)
        flux.kvs.put_unlink(self.f, "txn_unlink")
        flux.kvs.commit(self.f)
        self.assertFalse(flux.kvs.exists(self.f, "txn_unlink"))

    def test_api_10_put_symlink(self):
        flux.kvs.put_symlink(self.f, "txn_symlink", "txn_target")
        flux.kvs.commit(self.f)
        self.assertFalse(flux.kvs.exists(self.f, "txn_symlink"))

    def test_api_11_commit_flags(self):
        flux.kvs.put(self.f, "flagcheck", 42)
        flux.kvs.commit(self.f, 1)
        self.assertTrue(flux.kvs.exists(self.f, "flagcheck"))

    # just testing that passing flags work, these are pitiful KVS
    # changes and the flags don't mean much
    def test_api_12_commit_flags(self):
        flux.kvs.put(self.f, "commitflags", "foo")
        flux.kvs.commit(self.f, flux.constants.FLUX_KVS_NO_MERGE)
        flux.kvs.put(self.f, "commitflags", "baz")
        flux.kvs.commit(self.f, flux.constants.FLUX_KVS_TXN_COMPACT)
        flux.kvs.put(self.f, "commitflags", "bar")
        flux.kvs.commit(self.f, flux.constants.FLUX_KVS_SYNC)

    # try to overwrite root dir, will fail on commit
    def test_api_13_commit_fail(self):
        with self.assertRaises(OSError) as ctx:
            flux.kvs.put(self.f, ".", "foof")
            flux.kvs.commit(self.f)
        self.assertEqual(ctx.exception.errno, errno.EINVAL)

        # Issue #5333, make sure internal bad transaction cleared and
        # subsequent commit works
        flux.kvs.commit(self.f)

    def bad_input(self, func, *args):
        with self.assertRaises(OSError) as ctx:
            func(*args)
        self.assertEqual(ctx.exception.errno, errno.EINVAL)

    def test_bad_input_01_exists(self):
        self.bad_input(flux.kvs.exists, self.f, "")

    def test_bad_input_02_isdir(self):
        self.bad_input(flux.kvs.isdir, self.f, "")

    def test_bad_input_03_get(self):
        self.bad_input(flux.kvs.get, self.f, "")

    def test_bad_input_04_get_dir(self):
        self.bad_input(flux.kvs.get_dir, self.f, "")

    def test_bad_input_05_put_exception(self):
        self.bad_input(flux.kvs.put, self.f, "", "")

    def test_bad_input_07_put_raw_exception(self):
        with self.assertRaises(AttributeError):
            flux.kvs.put(self.f, "legit_key", 1, raw=True)

    def test_bad_input_08_put_mkdir_exception(self):
        self.bad_input(flux.kvs.put_mkdir, self.f, "")

    def test_bad_input_09_put_unlink(self):
        self.bad_input(flux.kvs.put_unlink, self.f, "")

    def test_bad_input_10_put_symlink(self):
        self.bad_input(flux.kvs.put_symlink, self.f, "", "")

    def test_kvsdir_01_bad_init(self):
        with self.assertRaises(ValueError):
            flux.kvs.KVSDir()

    def test_kvsdir_02_read_non_existent(self):
        with self.assertRaises(KeyError):
            print(
                flux.kvs.KVSDir(self.f)[
                    "crazykeythatclearlydoesntexistandneverwillinanyuniverse"
                ]
            )

    def test_kvsdir_03_read_non_existent_basedir(self):
        with self.assertRaisesRegex(EnvironmentError, "No such file"):
            print(
                flux.kvs.KVSDir(
                    self.f, "crazykeythatclearlydoesntexistandneverwillinanyuniverse"
                )
            )

    def test_kvsdir_04_remove(self):
        kd = self.set_and_check_context("todel", "things to delete", str)
        del kd["todel"]
        kd.commit()
        with self.assertRaises(KeyError):
            stuff = kd["todel"]
            print(stuff)

    def test_kvsdir_05_fill(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.fill({"things": 1, "stuff": "strstuff", "dir.other_thing": "dirstuff"})
            kd.commit()

        with flux.kvs.get_dir(self.f) as kd2:
            self.assertEqual(kd2["things"], 1)
            self.assertEqual(kd2["stuff"], "strstuff")
            self.assertEqual(kd2["dir"]["other_thing"], "dirstuff")

    def test_kvsdir_06_mkdir_fill(self):
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

    def test_kvsdir_07_key_at(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("testkeyat")
        with flux.kvs.get_dir(self.f, "testkeyat") as kd:
            self.assertEqual(kd.key_at("meh"), "testkeyat.meh")

    def test_kvsdir_08_exists_initial_path(self):
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

    def test_kvsdir_09_key_initial_path(self):
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

    def test_kvsdir_10_unlink_initial_path(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("unlinkinitialpath")
            kd["unlinkinitialpath"]["a"] = 1

        kd2 = flux.kvs.KVSDir(self.f)
        self.assertEqual(kd2["unlinkinitialpath.a"], 1)

        kd3 = flux.kvs.KVSDir(self.f, "unlinkinitialpath")
        del kd3["a"]
        kd3.commit()

        kd4 = flux.kvs.KVSDir(self.f)
        with self.assertRaises(KeyError):
            kd4["unlinkinitialpath.a"]

    def test_kvsdir_11_fill_initial_path(self):
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

    def test_kvsdir_12_mkdir_initial_path(self):
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

    def test_kvsdir_13_iterator(self):
        keys = ["testdir1a." + str(x) for x in range(1, 15)]
        with flux.kvs.get_dir(self.f) as kd:
            for k in keys:
                kd[k] = "bar"

        with flux.kvs.get_dir(self.f, "testdir1a") as kd:
            print(kd.keys())
            for k, v in kd.items():
                self.assertEqual(v, "bar")
                print("passed {}".format(k))

    def test_kvsdir_14_files(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("filestest", {"somefile": 1, "somefile2": 2})
            kd.mkdir("filestest.subdir")
            kd.commit()

        with flux.kvs.get_dir(self.f, "filestest") as kd2:
            files = [x for x in kd2.files()]
            self.assertEqual(len(files), 2)
            self.assertIn("somefile", files)
            self.assertIn("somefile2", files)

    def test_kvsdir_15_directories(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("directoriestest", {"somefile": 1, "somefile2": 2})
            kd.mkdir("directoriestest.subdir")
            kd.commit()

        with flux.kvs.get_dir(self.f, "directoriestest") as kd2:
            directories = [x for x in kd2.directories()]
            self.assertEqual(len(directories), 1)
            self.assertIn("subdir", directories)

    def test_kvsdir_16_list_all(self):
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

    def test_misc_01_walk(self):
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

    def test_misc_02_walk_with_no_handle(self):
        with self.assertRaises(ValueError):
            flux.kvs.walk("dir").next()

    # N.B. namespace tests may depend on prior tests creating
    # namespaces and data within those namespaces.  So order matters
    # in these tests and the numbering should be kept to enforce that
    # order.

    def test_namespace_01_namespace_list(self):
        nslist = flux.kvs.namespace_list(self.f)
        self.assertIn("primary", nslist)

    def test_namespace_02_namespace_create(self):
        flux.kvs.namespace_create(self.f, "testns1")
        flux.kvs.namespace_create(self.f, "testns2")
        flux.kvs.namespace_create(self.f, "testns3")
        nslist = flux.kvs.namespace_list(self.f)
        self.assertIn("testns1", nslist)
        self.assertIn("testns2", nslist)
        self.assertIn("testns3", nslist)

    def test_namespace_03_namespace_remove(self):
        flux.kvs.namespace_remove(self.f, "testns3")
        # namespace removal is eventually consistent, may need to
        # wait a bit to confirm.
        removed = False
        for i in range(30):
            nslist = flux.kvs.namespace_list(self.f)
            if "testns3" not in nslist:
                removed = True
                break
        self.assertTrue(removed)

    def test_namespace_04_commit(self):
        flux.kvs.put_mkdir(self.f, "testdirns1")
        flux.kvs.put(self.f, "testdirns1.a", 1)
        flux.kvs.commit(self.f, namespace="testns1")
        flux.kvs.put_mkdir(self.f, "testdirns2")
        flux.kvs.put(self.f, "testdirns2.a", 2)
        flux.kvs.commit(self.f, namespace="testns2")

    def test_namespace_05_get(self):
        self.assertEqual(flux.kvs.get(self.f, "testdirns1.a", namespace="testns1"), 1)
        self.assertEqual(flux.kvs.get(self.f, "testdirns2.a", namespace="testns2"), 2)
        with self.assertRaises(OSError) as cm:
            flux.kvs.get(self.f, "testdirns1.a", namespace="testns2")
        self.assertEqual(cm.exception.errno, errno.ENOENT)
        with self.assertRaises(OSError) as cm:
            flux.kvs.get(self.f, "testdirns2.a", namespace="testns1")
        self.assertEqual(cm.exception.errno, errno.ENOENT)

    def test_namespace_06_exists(self):
        self.assertTrue(flux.kvs.exists(self.f, "testdirns1", namespace="testns1"))
        self.assertTrue(flux.kvs.exists(self.f, "testdirns1.a", namespace="testns1"))
        self.assertFalse(flux.kvs.exists(self.f, "testdirns1", namespace="testns2"))
        self.assertFalse(flux.kvs.exists(self.f, "testdirns1.a", namespace="testns2"))

        self.assertFalse(flux.kvs.exists(self.f, "testdirns2", namespace="testns1"))
        self.assertFalse(flux.kvs.exists(self.f, "testdirns2.a", namespace="testns1"))
        self.assertTrue(flux.kvs.exists(self.f, "testdirns2", namespace="testns2"))
        self.assertTrue(flux.kvs.exists(self.f, "testdirns2.a", namespace="testns2"))

    def test_namespace_06_isdir(self):
        self.assertTrue(flux.kvs.isdir(self.f, "testdirns1", namespace="testns1"))
        self.assertFalse(flux.kvs.isdir(self.f, "testdirns1.a", namespace="testns1"))
        self.assertFalse(flux.kvs.isdir(self.f, "testdirns1", namespace="testns2"))
        self.assertFalse(flux.kvs.isdir(self.f, "testdirns1.a", namespace="testns2"))

        self.assertFalse(flux.kvs.isdir(self.f, "testdirns2", namespace="testns1"))
        self.assertFalse(flux.kvs.isdir(self.f, "testdirns2.a", namespace="testns1"))
        self.assertTrue(flux.kvs.isdir(self.f, "testdirns2", namespace="testns2"))
        self.assertFalse(flux.kvs.isdir(self.f, "testdirns2.a", namespace="testns2"))

    def test_namespace_07_unlink(self):
        flux.kvs.namespace_create(self.f, "testnsunlink")
        flux.kvs.put(self.f, "todelete", 1)
        flux.kvs.commit(self.f, namespace="testnsunlink")
        self.assertTrue(flux.kvs.exists(self.f, "todelete", namespace="testnsunlink"))

        flux.kvs.put_unlink(self.f, "todelete")
        flux.kvs.commit(self.f, namespace="testnsunlink")

        with self.assertRaises(OSError) as cm:
            flux.kvs.get(self.f, "todelete", namespace="testnsunlink")
        self.assertEqual(cm.exception.errno, errno.ENOENT)

    def test_namespace_08_KVSDir_fill(self):
        flux.kvs.namespace_create(self.f, "testnsfill")
        with flux.kvs.get_dir(self.f, namespace="testnsfill") as kd:
            kd.fill({"testdirnsfill.a": 1})
            kd.fill({"testdirnsfill.b": 2})
            kd.commit()

        self.assertTrue(flux.kvs.isdir(self.f, "testdirnsfill", namespace="testnsfill"))
        self.assertTrue(
            flux.kvs.exists(self.f, "testdirnsfill.a", namespace="testnsfill")
        )
        self.assertTrue(
            flux.kvs.exists(self.f, "testdirnsfill.b", namespace="testnsfill")
        )

    def test_namespace_09_KVSDir_mkdir_fill(self):
        flux.kvs.namespace_create(self.f, "testnsmkdirfill")
        with flux.kvs.get_dir(self.f, namespace="testnsmkdirfill") as kd:
            kd.mkdir("testdirnsmkdirfill", {"a": 1})
            kd.commit()

        self.assertTrue(
            flux.kvs.isdir(self.f, "testdirnsmkdirfill", namespace="testnsmkdirfill")
        )
        self.assertTrue(
            flux.kvs.exists(self.f, "testdirnsmkdirfill.a", namespace="testnsmkdirfill")
        )

    def test_namespace_10_KVSDir_initial_path(self):
        with flux.kvs.get_dir(self.f, "testdirns1", namespace="testns1") as kd:
            self.assertTrue(kd.exists("a"))
            kd["b"] = 2

        with flux.kvs.get_dir(self.f, namespace="testns1") as kd2:
            self.assertTrue(kd2.exists("testdirns1.a"))
            self.assertTrue(kd2.exists("testdirns1.b"))

        self.assertFalse(flux.kvs.exists(self.f, "testdirns1.b", namespace="testns2"))

    def test_namespace_11_KVSDir_files(self):
        with flux.kvs.get_dir(self.f, "testdirns1", namespace="testns1") as kd:
            files = [x for x in kd.files()]
            self.assertEqual(len(files), 2)
            self.assertIn("a", files)
            self.assertIn("b", files)

        with flux.kvs.get_dir(self.f, "testdirns2", namespace="testns2") as kd2:
            files = [x for x in kd2.files()]
            self.assertEqual(len(files), 1)
            self.assertIn("a", files)

    def test_namespace_12_KVSDir_directories(self):
        with flux.kvs.get_dir(self.f, namespace="testns1") as kd:
            directories = [x for x in kd.directories()]
            self.assertEqual(len(directories), 1)
            self.assertIn("testdirns1", directories)

        with flux.kvs.get_dir(self.f, namespace="testns2") as kd2:
            directories = [x for x in kd2.directories()]
            self.assertEqual(len(directories), 1)
            self.assertIn("testdirns2", directories)

    def test_namespace_13_KVSDir_list_all(self):
        with flux.kvs.get_dir(self.f, namespace="testns1") as kd:
            (files, directories) = kd.list_all()
            self.assertEqual(len(files), 0)
            self.assertEqual(len(directories), 1)

        with flux.kvs.get_dir(self.f, namespace="testns2") as kd2:
            (files, directories) = kd2.list_all()
            self.assertEqual(len(files), 0)
            self.assertEqual(len(directories), 1)

    def test_namespace_14_walk(self):
        walk_gen = flux.kvs.walk(
            ".", flux_handle=self.f, topdown=True, namespace="testns1"
        )
        (r, ds, fs) = next(walk_gen)
        self.assertEqual(r, "")
        self.assertEqual(len(list(ds)), 1)
        self.assertEqual(len(list(fs)), 0)

        walk_gen = flux.kvs.walk(
            "testdirns1", flux_handle=self.f, topdown=True, namespace="testns1"
        )
        (r, ds, fs) = next(walk_gen)
        self.assertEqual(r, "")
        self.assertEqual(len(list(ds)), 0)
        self.assertEqual(len(list(fs)), 2)

    # N.B. some kvstxn tests depend on prior tests creating dirs/keys.
    # Thus tests are numbered to ensure they are performed in order.

    def test_kvstxn_01_basic(self):
        txn = flux.kvs.KVSTxn(self.f)
        txn.mkdir("kvstxntestdir")
        txn.put("kvstxntestdir.a", 1)
        txn.put("kvstxntestdir.b", 2)
        txn.put("kvstxntestdir.c", "foobar".encode("utf-8"), raw=True)
        txn.put("kvstxntestdir.d", "boobar", raw=True)
        txn.symlink("symlink", "kvstxntestdir.a")
        txn.commit()

        self.assertTrue(flux.kvs.isdir(self.f, "kvstxntestdir"))
        self.assertEqual(flux.kvs.get(self.f, "kvstxntestdir.a"), 1)
        self.assertEqual(flux.kvs.get(self.f, "kvstxntestdir.b"), 2)
        self.assertEqual(flux.kvs.get(self.f, "kvstxntestdir.c"), "foobar")
        self.assertEqual(flux.kvs.get(self.f, "kvstxntestdir.d"), "boobar")
        self.assertEqual(flux.kvs.get(self.f, "symlink"), 1)

        txn.unlink("kvstxntestdir.b")
        txn.commit()

        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestdir.b"))

    def test_kvstxn_02_initial_path(self):
        txn = flux.kvs.KVSTxn(self.f, "kvstxntestdir")
        txn.put("e", 3)
        txn.commit()

        self.assertEqual(flux.kvs.get(self.f, "kvstxntestdir.e"), 3)

        txn.unlink("e")
        txn.commit()

        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestdir.e"))

    def test_kvstxn_03_namespace(self):
        flux.kvs.namespace_create(self.f, "testns")

        txn = flux.kvs.KVSTxn(self.f, namespace="testns")
        txn.put("nskey", 1)
        txn.commit()

        self.assertTrue(flux.kvs.exists(self.f, "nskey", namespace="testns"))
        self.assertFalse(flux.kvs.exists(self.f, "nskey"))

    def test_kvstxn_04_context_manager(self):
        with flux.kvs.KVSTxn(self.f) as txn:
            txn.put("kvstxntestdir.f", 4)

        self.assertEqual(flux.kvs.get(self.f, "kvstxntestdir.f"), 4)

        with flux.kvs.KVSTxn(self.f) as txn2:
            txn2.unlink("kvstxntestdir.f")

        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestdir.f"))

    def test_kvstxn_05_pass_to_api(self):
        # N.B. generally user should never do this, testing to ensure
        # logic works.
        txn = flux.kvs.KVSTxn(self.f)
        flux.kvs.put_mkdir(self.f, "kvstxntestapi", _kvstxn=txn)
        flux.kvs.put(self.f, "kvstxntestapi.a", 1, _kvstxn=txn)
        flux.kvs.put(self.f, "kvstxntestapi.b", 2, _kvstxn=txn)
        flux.kvs.put_symlink(self.f, "symlink", "kvstxntestapi.a", _kvstxn=txn)
        flux.kvs.commit(self.f, _kvstxn=txn)

        self.assertTrue(flux.kvs.isdir(self.f, "kvstxntestapi"))
        self.assertEqual(flux.kvs.get(self.f, "kvstxntestapi.a"), 1)
        self.assertEqual(flux.kvs.get(self.f, "kvstxntestapi.b"), 2)
        self.assertEqual(flux.kvs.get(self.f, "symlink"), 1)

        flux.kvs.put_unlink(self.f, "kvstxntestapi.b", _kvstxn=txn)
        flux.kvs.commit(self.f, _kvstxn=txn)

        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestapi.b"))

    def test_kvstxn_06_multiple(self):
        with flux.kvs.KVSTxn(self.f) as txn:
            txn.mkdir("kvstxntestmulti")

        txn1 = flux.kvs.KVSTxn(self.f)
        txn1.put("kvstxntestmulti.a", 1)
        txn1.put("kvstxntestmulti.c", 3)

        txn2 = flux.kvs.KVSTxn(self.f)
        txn2.put("kvstxntestmulti.b", 2)
        txn2.put("kvstxntestmulti.d", 4)

        txn1.commit()

        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmulti.a"))
        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestmulti.b"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmulti.c"))
        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestmulti.d"))

        txn2.commit()

        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmulti.a"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmulti.b"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmulti.c"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmulti.d"))

    def test_kvstxn_06_multiple_kvsdir(self):
        with flux.kvs.KVSTxn(self.f) as txn:
            txn.mkdir("kvstxntestmultikvsdir")

        kvsdir1 = flux.kvs.get_dir(self.f, "kvstxntestmultikvsdir")
        kvsdir1["a"] = 1
        kvsdir1["c"] = 3

        kvsdir2 = flux.kvs.get_dir(self.f, "kvstxntestmultikvsdir")
        kvsdir2["b"] = 2
        kvsdir2["d"] = 4

        kvsdir1.commit()

        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.a"))
        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.b"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.c"))
        self.assertFalse(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.d"))

        kvsdir2.commit()

        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.a"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.b"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.c"))
        self.assertTrue(flux.kvs.exists(self.f, "kvstxntestmultikvsdir.d"))

    # this test ensures subdirs get the same internal transaction of
    # the parent KVSDir object, therefore only a single commit() call is
    # necessary
    def test_kvstxn_07_kvsdir_subdir(self):
        with flux.kvs.KVSTxn(self.f) as txn:
            txn.mkdir("kvstxntestsubdir")
            txn.mkdir("kvstxntestsubdir.subdir")

        kvsdir = flux.kvs.get_dir(
            self.f,
        )
        subdir = kvsdir["kvstxntestsubdir"]["subdir"]
        subdir["a"] = 1

        kvsdir.commit()

        self.assertEqual(flux.kvs.get(self.f, "kvstxntestsubdir.subdir.a"), 1)

    # this test ensures commits are not done when an object reference
    # count goes to 0
    def test_kvstxn_08_kvsdir_keep_reference(self):
        with flux.kvs.KVSTxn(self.f) as txn:
            txn.mkdir("kvstxntestkeepref")
            txn.mkdir("kvstxntestkeepref.dir1")
            txn.mkdir("kvstxntestkeepref.dir1.dir2")

        kvsdir = flux.kvs.get_dir(
            self.f,
        )
        kvsdir["kvstxntestkeepref"]["dir1"]["dir2"]["a"] = 1
        # N.B. flake8 check, F841 = "assigned to but never used"
        keepref1 = kvsdir["kvstxntestkeepref"]  # noqa: F841
        keepref2 = kvsdir["kvstxntestkeepref"]["dir1"]  # noqa: F841
        keepref3 = kvsdir["kvstxntestkeepref"]["dir1"]["dir2"]  # noqa: F841

        kvsdir.commit()

        self.assertEqual(flux.kvs.get(self.f, "kvstxntestkeepref.dir1.dir2.a"), 1)

    def test_commic_async_01_basic(self):
        flux.kvs.put(self.f, "commicasync1", "foo")
        f = flux.kvs.commit_async(self.f)
        f.get()
        self.assertEqual(flux.kvs.get(self.f, "commicasync1"), "foo")

    def test_commic_async_02_with_kvstxn(self):
        txn = flux.kvs.KVSTxn(self.f)
        txn.put("commitasync2", "bar")

        f = flux.kvs.commit_async(self.f, _kvstxn=txn)
        f.get()
        self.assertEqual(flux.kvs.get(self.f, "commitasync2"), "bar")

    def test_commic_async_03_kvstxn(self):
        txn = flux.kvs.KVSTxn(self.f)
        txn.put("commitasync3", "baz")
        f = txn.commit_async()
        f.get()
        self.assertEqual(flux.kvs.get(self.f, "commitasync3"), "baz")

    def test_commic_async_04_kvsdir(self):
        kd = flux.kvs.KVSDir(self.f)
        kd["commitasync4"] = "qux"
        f = kd.commit_async()
        f.get()
        self.assertEqual(flux.kvs.get(self.f, "commitasync4"), "qux")

    def test_kvswatch_01_key_ENOENT(self):
        with self.assertRaises(OSError) as cm:
            future = flux.kvs.kvs_watch_async(self.f, "grog")
            future.get()
        self.assertEqual(cm.exception.errno, errno.ENOENT)

    def _change_value_kvs_nowait(self, key, val):
        kvstxn = flux.kvs.KVSTxn(self.f)
        kvstxn.put(key, val)
        flux.kvs.commit_async(self.f, _kvstxn=kvstxn)

    def test_kvswatch_02_kvs_watch_async_basic(self):
        myarg = dict(a=1, b=2)
        vals = []

        def cb(future, arg):
            self.assertEqual(arg, myarg)
            val = future.get()
            if val is None:
                return
            elif val == 1:
                self._change_value_kvs_nowait("kvswatch1.val", 2)
            elif val == 2:
                future.cancel()
            vals.append(val)

        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("kvswatch1")
            kd["kvswatch1.val"] = 1

        future = flux.kvs.kvs_watch_async(self.f, "kvswatch1.val")
        self.assertIsInstance(future, flux.kvs.KVSWatchFuture)
        future.then(cb, myarg)
        rc = self.f.reactor_run()
        self.assertGreaterEqual(rc, 0)
        self.assertEqual(len(vals), 2)
        self.assertEqual(vals[0], 1)
        self.assertEqual(vals[1], 2)

    def test_kvswatch_03_kvs_watch_async_no_autoreset(self):
        vals = []

        def cb(future, arg):
            val = future.get(autoreset=False)
            if val is None:
                return
            elif val == 1:
                valtmp = future.get()
                # Value hasn't changed
                self.assertEqual(valtmp, 1)
                self._change_value_kvs_nowait("kvswatch2.val", 2)
            elif val == 2:
                valtmp = future.get()
                # Value hasn't changed
                self.assertEqual(valtmp, 2)
                future.cancel()
            vals.append(val)

        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("kvswatch2")
            kd["kvswatch2.val"] = 1

        future = flux.kvs.kvs_watch_async(self.f, "kvswatch2.val")
        self.assertIsInstance(future, flux.kvs.KVSWatchFuture)
        future.then(cb, None)
        rc = self.f.reactor_run()
        self.assertGreaterEqual(rc, 0)
        self.assertEqual(len(vals), 2)
        self.assertEqual(vals[0], 1)
        self.assertEqual(vals[1], 2)

    def test_kvswatch_04_kvs_watch_async_waitcreate(self):
        # To test waitcreate, we create two KVS watchers, one with
        # waitcreate and one without.  The one without waitcreate will
        # create the field we are waiting to be created.
        results = []

        def cb_ENOENT(future, arg):
            try:
                future.get()
            except OSError as e:
                self.assertEqual(e.errno, errno.ENOENT)
                self._change_value_kvs_nowait("kvswatch3.val", 1)
                results.append("ENOENT")

        def cb_waitcreate(future, arg):
            val = future.get()
            if val is None:
                return
            elif val == 1:
                future.cancel()
            results.append(val)

        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("kvswatch3")

        future1 = flux.kvs.kvs_watch_async(self.f, "kvswatch3.val")
        future2 = flux.kvs.kvs_watch_async(self.f, "kvswatch3.val", waitcreate=True)
        future1.then(cb_ENOENT, None)
        future2.then(cb_waitcreate, None)
        rc = self.f.reactor_run()
        self.assertGreaterEqual(rc, 0)
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0], "ENOENT")
        self.assertEqual(results[1], 1)

    def test_kvswatch_05_kvs_watch_async_uniq(self):
        # To test uniq, we create two KVS watchers, one with uniq and
        # one without.  The one with uniq should see fewer changes
        # than the one without.
        vals = []
        uniq_vals = []

        def cb(future, arg):
            otherfuture = arg
            val = future.get()
            if val is None:
                return
            elif val == 1:
                if len(vals) == 0:
                    self._change_value_kvs_nowait("kvswatch4.val", 1)
                elif len(vals) == 1:
                    self._change_value_kvs_nowait("kvswatch4.val", 2)
            vals.append(val)
            if len(vals) == 3 and len(uniq_vals) == 2:
                future.cancel()
                otherfuture.cancel()

        def cb_uniq(future, arg):
            otherfuture = arg
            val = future.get()
            if val is None:
                return
            uniq_vals.append(val)
            if len(vals) == 3 and len(uniq_vals) == 2:
                future.cancel()
                otherfuture.cancel()

        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("kvswatch4")
            kd["kvswatch4.val"] = 1

        future1 = flux.kvs.kvs_watch_async(self.f, "kvswatch4.val")
        future2 = flux.kvs.kvs_watch_async(self.f, "kvswatch4.val", uniq=True)
        future1.then(cb, future2)
        future2.then(cb_uniq, future1)
        rc = self.f.reactor_run()
        self.assertGreaterEqual(rc, 0)
        self.assertEqual(len(vals), 3)
        self.assertEqual(vals[0], 1)
        self.assertEqual(vals[1], 1)
        self.assertEqual(vals[2], 2)
        self.assertEqual(len(uniq_vals), 2)
        self.assertEqual(uniq_vals[0], 1)
        self.assertEqual(uniq_vals[1], 2)

    def test_kvswatch_06_kvs_watch_async_full(self):
        # To test full, we create two KVS watchers, one with full and
        # one without.  The one with full should see a change if we
        # delete an upstream directory.  The other watcher should not.
        vals = []
        full_vals = []

        def cb(future, arg):
            val = future.get()
            if val is None:
                return
            vals.append(val)

        def cb_full(future, arg):
            otherfuture = arg
            try:
                val = future.get()
                if val is None:
                    return
                elif val == 1:
                    # Set to None == unlink/delete/remove
                    self._change_value_kvs_nowait("kvswatch5", None)
            except OSError:
                future.cancel()
                otherfuture.cancel()
                full_vals.append("ENOENT")
                return
            full_vals.append(val)

        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir("kvswatch5")
            kd["kvswatch5.val"] = 1

        future1 = flux.kvs.kvs_watch_async(self.f, "kvswatch5.val")
        future2 = flux.kvs.kvs_watch_async(self.f, "kvswatch5.val", full=True)
        future1.then(cb, None)
        future2.then(cb_full, future1)
        rc = self.f.reactor_run()
        self.assertGreaterEqual(rc, 0)
        self.assertEqual(len(vals), 1)
        self.assertEqual(vals[0], 1)
        self.assertEqual(len(full_vals), 2)
        self.assertEqual(full_vals[0], 1)
        self.assertEqual(full_vals[1], "ENOENT")

    def test_kvs_checkpoint_01_lookup(self):
        c = flux.kvs.kvs_checkpoint_lookup(self.f)
        self.assertEqual(c[0]["version"], 1)
        self.assertIn("sequence", c[0])
        self.assertIn("timestamp", c[0])
        self.assertIn("rootref", c[0])

        c = flux.kvs.kvs_checkpoint_lookup(self.f, cache_bypass=True)
        self.assertEqual(c[0]["version"], 1)
        self.assertIn("sequence", c[0])
        self.assertIn("timestamp", c[0])
        self.assertIn("rootref", c[0])


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner())
