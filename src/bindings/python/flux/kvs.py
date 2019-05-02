###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from __future__ import print_function

import json
import errno
import collections

from _flux._core import ffi, lib
from flux.wrapper import Wrapper, WrapperPimpl

try:
    # pylint: disable=invalid-name
    collectionsAbc = collections.abc
except AttributeError:
    # pylint: disable=invalid-name
    collectionsAbc = collections


class KVSWrapper(Wrapper):
    # This empty class accepts new methods, preventing accidental overloading
    # across wrappers
    pass


RAW = KVSWrapper(ffi, lib, prefixes=["flux_kvs", "flux_kvs_"])
# override error check behavior for flux_kvsitr_next
RAW.flux_kvsitr_next.set_error_check(lambda x: False)


def get_key_direct(flux_handle, key):
    valp = ffi.new("char *[1]")
    future = RAW.flux_kvs_lookup(flux_handle, None, 0, key)
    RAW.flux_kvs_lookup_get(future, valp)
    if valp[0] == ffi.NULL:
        return None

    ret = json.loads(ffi.string(valp[0]).decode("utf-8"))
    RAW.flux_future_destroy(future)
    return ret


def exists(flux_handle, key):
    try:
        get_key_direct(flux_handle, key)
        return True
    except EnvironmentError as err:
        if err.errno == errno.ENOENT:
            return False
        if err.errno == errno.EISDIR:
            return True
        raise err


def isdir(flux_handle, key):
    try:
        get_key_direct(flux_handle, key)
    except EnvironmentError as err:
        if err.errno == errno.EISDIR:
            return True
        raise err
    return False


def get_dir(flux_handle, key="."):
    return KVSDir(path=key, flux_handle=flux_handle)


def get(flux_handle, key):
    try:
        return get_key_direct(flux_handle, key)
    except EnvironmentError as err:
        if err.errno == errno.EISDIR:
            pass
        else:
            raise err
    return get_dir(flux_handle, key)


def put(flux_handle, key, value):
    json_str = json.dumps(value)
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    return RAW.flux_kvs_txn_put(flux_handle.aux_txn, 0, key, json_str)


def put_mkdir(flux_handle, key):
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    return RAW.flux_kvs_txn_mkdir(flux_handle.aux_txn, 0, key)


def put_unlink(flux_handle, key):
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    return RAW.flux_kvs_txn_unlink(flux_handle.aux_txn, 0, key)


def put_symlink(flux_handle, key, target):
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    return RAW.flux_kvs_txn_symlink(flux_handle.aux_txn, 0, key, None, target)


def commit(flux_handle, flags=0):
    if flux_handle.aux_txn is None:
        return -1
    future = RAW.flux_kvs_commit(flux_handle, None, flags, flux_handle.aux_txn)
    RAW.flux_future_get(future, None)
    RAW.flux_kvs_txn_destroy(flux_handle.aux_txn)
    flux_handle.aux_txn = None
    return 0


def dropcache(flux_handle):
    return RAW.flux_kvs_dropcache(flux_handle)


class KVSDir(WrapperPimpl, collectionsAbc.MutableMapping):
    # pylint: disable=too-many-ancestors, too-many-public-methods

    class InnerWrapper(Wrapper):

        # pylint: disable=no-value-for-parameter
        def __init__(self, flux_handle=None, path=".", handle=None):
            dest = RAW.flux_kvsdir_destroy
            super(KVSDir.InnerWrapper, self).__init__(
                ffi,
                lib,
                handle=handle,
                match=ffi.typeof("flux_kvsdir_t *"),
                prefixes=["flux_kvsdir_"],
                destructor=dest,
            )

            if flux_handle is None and handle is None:  # pragma: no cover
                raise ValueError(
                    "flux_handle must be a valid Flux object or "
                    "handle must be a valid kvsdir cdata pointer"
                )
            if handle is None:
                directory = ffi.new("flux_kvsdir_t *[1]")
                future = RAW.flux_kvs_lookup(
                    flux_handle, None, RAW.FLUX_KVS_READDIR, path
                )
                RAW.flux_kvs_lookup_get_dir(future, directory)
                self.handle = RAW.flux_kvsdir_copy(directory[0])
                RAW.flux_future_destroy(future)
                if self.handle is None or self.handle == ffi.NULL:
                    raise EnvironmentError("No such file or directory")

    def __init__(self, flux_handle=None, path=".", handle=None):
        super(KVSDir, self).__init__()
        self.fhdl = flux_handle
        self.path = path
        if flux_handle is None and handle is None:
            raise ValueError(
                "flux_handle must be a valid Flux object or"
                "handle must be a valid kvsdir cdata pointer"
            )
        self.pimpl = self.InnerWrapper(flux_handle, path, handle)

    def commit(self, flags=0):
        return commit(self.fhdl, flags)

    def key_at(self, key):
        p_str = self.pimpl.key_at(key)
        return p_str.decode("utf-8")

    def exists(self, name):
        return exists(self.fhdl, name)

    def __getitem__(self, key):
        try:
            return get(self.fhdl, self.key_at(key))
        except EnvironmentError:
            raise KeyError(
                "{} not found under directory {}".format(key, self.key_at(""))
            )

    def __setitem__(self, key, value):
        if put(self.fhdl, key, value) < 0:
            print("Error setting item in KVS")

    def __delitem__(self, key):
        put_unlink(self.fhdl, key)

    class KVSDirIterator(collectionsAbc.Iterator):
        def __init__(self, kvsdir):
            super(KVSDir.KVSDirIterator, self).__init__()
            self.kvsdir = kvsdir
            self.itr = None
            self.itr = RAW.flux_kvsitr_create(kvsdir.handle)

        def __del__(self):
            RAW.flux_kvsitr_destroy(self.itr)

        def __iter__(self):
            return self

        def __next__(self):
            ret = RAW.flux_kvsitr_next(self.itr)
            if ret is None or ret == ffi.NULL:
                raise StopIteration()
            return ret.decode("utf-8")

        def next(self):
            return self.__next__()

    def __iter__(self):
        return self.KVSDirIterator(self)

    def __len__(self):
        return self.pimpl.get_size()

    def fill(self, contents):
        """
        Populate this directory with keys specified by contents, which must
        conform to the Mapping interface

        :param contents: A dict of keys and values to be created in the
        directory or None, sub-directories can be created by using `dir.file`
        syntax, sub-dicts will be stored as json values in a single key
        """

        if contents is None:
            raise ValueError("contents must be non-None")

        try:
            for key, val in contents.items():
                self[key] = val
        finally:
            self.commit()

    def mkdir(self, key, contents=None):
        """
        Create a new sub-directory, optionally pre-populated with the contents
        of `files` as would be done with `fill(contents)`

        :param key: Key of the directory to be created
        :param contents: A dict of keys and values to be created in the
          directory or None, sub-directories can be created by using `dir.file`
          syntax, sub-dicts will be stored as json values in a single key
        """

        put_mkdir(self.fhdl, key)
        self.commit()
        new_kvsdir = KVSDir(self.fhdl, key)
        if contents is not None:
            new_kvsdir.fill(contents)

    def files(self):
        for k in self:
            if not self.pimpl.isdir(k):
                yield k

    def directories(self):
        for k in self:
            if self.pimpl.isdir(k):
                yield k

    def list_all(self):
        files = []
        dirs = []
        for k in self:
            if self.pimpl.isdir(k):
                dirs.append(k)
            else:
                files.append(k)
        return (files, dirs)

    def __enter__(self):
        """Allow this to be used as a context manager"""
        return self

    def __exit__(self, type_arg, value, tb):
        """
        When used as a context manager, the KVSDir commits itself on exit
        """
        self.commit()
        return False


def join(*args):
    return ".".join([a for a in args if len(a) > 0])


def inner_walk(kvsdir, curr_dir, topdown=False):
    if topdown:
        yield (curr_dir, kvsdir.directories(), kvsdir.files())

    for directory in kvsdir.directories():
        path = join(curr_dir, directory)
        key = kvsdir.key_at(directory)
        for entry in inner_walk(get_dir(kvsdir.fhdl, key), path, topdown):
            yield entry

    if not topdown:
        yield (curr_dir, kvsdir.directories(), kvsdir.files())


def walk(directory, topdown=False, flux_handle=None):
    """ Walk a directory in the style of os.walk() """
    if not isinstance(directory, KVSDir):
        if flux_handle is None:
            raise ValueError("If directory is a key, flux_handle must be specified")
        directory = KVSDir(flux_handle, directory)
    return inner_walk(directory, "", topdown)
