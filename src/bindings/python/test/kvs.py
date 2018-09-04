#!/usr/bin/env python
from __future__ import print_function
import unittest
import errno
import os
import sys
import flux.core as core
import flux
import flux.kvs
import json
from pycotap import TAPTestRunner
from sideflux import run_beside_flux


def __flux_size():
    return 2


class TestKVS(unittest.TestCase):
    def setUp(self):
        """Create a handle, connect to flux"""
        self.f = core.Flux()

    def test_kvs_dir_open(self):
        with flux.kvs.get_dir(self.f) as d:
            self.assertIsNotNone(d)

    def test_kvs_dir_open(self):
        with flux.kvs.get_dir(self.f) as d:
            self.assertIsNotNone(d)

    def set_and_check_context(self, key, value, msg=''):
        kd = flux.kvs.KVSDir(self.f)
        kd[key] = value
        kd.commit()
        nv = kd[key]
        self.assertEqual(value, nv)
        return kd

    def test_set_int(self):
        self.set_and_check_context('int', 10)

    def test_set_float(self):
        self.set_and_check_context('float', 10.5)

    def test_set_string(self):
        self.set_and_check_context('string', "stuff")

    def test_set_list(self):
        self.set_and_check_context('list', [1, 2, 3, 4])

    def test_set_dict(self):
        self.set_and_check_context(
            'dict', {'thing': 'stuff',
                     'other thing': 'more stuff'})

    def test_exists_dir(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir('pytestdir')
        self.assertTrue(flux.kvs.exists(self.f, 'pytestdir'))

    def test_exists_true(self):
        flux.kvs.put(self.f, 'thing', 15)
        flux.kvs.commit(self.f)
        self.assertTrue(flux.kvs.exists(self.f, 'thing'))

    def test_exists_false(self):
        self.assertFalse(flux.kvs.exists(self.f, 'argbah'))

    def test_commit_flags(self):
        flux.kvs.put(self.f, 'flagcheck', 42)
        flux.kvs.commit(self.f, 1)
        self.assertTrue(flux.kvs.exists(self.f, 'flagcheck'))

    def test_remove(self):
        kd = self.set_and_check_context('todel', "things to delete")
        del kd['todel']
        kd.commit()
        with self.assertRaises(KeyError):
            stuff = kd['todel']
            print (stuff)

    def test_fill(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.fill({
                'things': 1,
                'stuff': 'strstuff',
                'dir.other_thing': 'dirstuff'
            })
            kd.commit()

            self.assertEqual(kd['things'], 1)
            self.assertEqual(kd['stuff'], 'strstuff')
            self.assertEqual(kd['dir']['other_thing'], 'dirstuff')

    def test_set_deep(self):
        self.set_and_check_context('a.b.c.e.f.j.k', 5)

    def test_bad_init(self):
        with self.assertRaises(ValueError):
            flux.kvs.KVSDir()

    def test_key_at(self):
        with flux.kvs.get_dir(self.f) as kd:
            kd.mkdir('testkeyat')
        with flux.kvs.get_dir(self.f, 'testkeyat') as kd:
            self.assertEqual(kd.key_at('meh'), 'testkeyat.meh')

    def test_walk_with_no_handle(self):
        with self.assertRaises(ValueError):
            flux.kvs.walk('dir').next()

    def test_read_non_existent(self):
        with self.assertRaises(KeyError):
            print (flux.kvs.KVSDir(self.f)[
                'crazykeythatclearlydoesntexistandneverwillinanyuniverse'
            ])

    def test_read_non_existent_basedir(self):
        with self.assertRaisesRegexp(EnvironmentError, "No such file"):
            print(flux.kvs.KVSDir(
                self.f,
                'crazykeythatclearlydoesntexistandneverwillinanyuniverse')
                  )

    def test_iterator(self):
        keys = ['testdir1a.' + str(x) for x in range(1, 15)]
        with flux.kvs.get_dir(self.f) as kd:
            for k in keys:
                kd[k] = "bar"

        with flux.kvs.get_dir(self.f, 'testdir1a') as kd:
            print(kd.keys())
            for k, v in kd.items():
                self.assertEqual(v, 'bar')
                print("passed {}".format(k))

    def test_walk(self):
        keys = ['testwalk.' + str(x) for x in range(1, 15)]
        with flux.kvs.get_dir(self.f) as kd:
            for k in keys:
                kd[k] = "bar"
                kd[k + 'd.' + k] = 'meh'
        walk_gen = flux.kvs.walk('testwalk', flux_handle=self.f, topdown=True)
        (r, ds, fs) = walk_gen.next()
        print(r, ds, fs)
        self.assertEqual(r, '')
        self.assertEqual(len(list(ds)), 14)
        self.assertEqual(len(list(fs)), 14)

        for r, ds, fs in walk_gen:
            pass
