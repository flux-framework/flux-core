###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import collections.abc as abc
import errno
import json
import os
from typing import Any, Mapping

from _flux._core import ffi, lib
from flux.rpc import RPC
from flux.wrapper import Wrapper, WrapperPimpl


class KVSWrapper(Wrapper):
    # This empty class accepts new methods, preventing accidental overloading
    # across wrappers
    pass


RAW = KVSWrapper(ffi, lib, prefixes=["flux_kvs", "flux_kvs_"])
# override error check behavior for flux_kvsitr_next
RAW.flux_kvsitr_next.set_error_check(lambda x: False)


def get_key_direct(flux_handle, key, namespace=None):
    valp = ffi.new("char *[1]")
    future = RAW.flux_kvs_lookup(flux_handle, namespace, 0, key)
    RAW.flux_kvs_lookup_get(future, valp)
    if valp[0] == ffi.NULL:
        return None

    try:
        ret = json.loads(ffi.string(valp[0]).decode("utf-8"))
    except json.decoder.JSONDecodeError:
        ret = ffi.string(valp[0]).decode("utf-8")
    except UnicodeDecodeError:
        ret = ffi.string(valp[0])
    RAW.flux_future_destroy(future)
    return ret


def exists(flux_handle, key, namespace=None):
    """Determine if key exists

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: key to check for existence

    Returns:
        bool: True if key exists, False if not
        namespace: namespace to read from, defaults to None.  If namespace
          is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.
    """
    try:
        get_key_direct(flux_handle, key, namespace=namespace)
        return True
    except EnvironmentError as err:
        if err.errno == errno.ENOENT:
            return False
        if err.errno == errno.EISDIR:
            return True
        raise err


def isdir(flux_handle, key, namespace=None):
    """Determine if key is a directory

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: key to check if it is a directory

    Returns:
        bool: True if key is a directory, False if not
        namespace: namespace to read from, defaults to None.  If namespace
          is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.
    """
    try:
        get_key_direct(flux_handle, key, namespace=namespace)
    except EnvironmentError as err:
        if err.errno == errno.ENOENT:
            return False
        if err.errno == errno.EISDIR:
            return True
        raise err
    return False


def get_dir(flux_handle, key=".", namespace=None):
    """Get KVS directory

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: directory name (default ".")
        namespace: namespace to read from, defaults to None.  If namespace
          is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.

    Returns:
        KVSDir: object representing directory
    """
    return KVSDir(path=key, flux_handle=flux_handle, namespace=namespace)


def get(flux_handle, key, namespace=None):
    """Get KVS directory

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: key to get
        namespace: namespace to read from, defaults to None.  If namespace
          is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.

    Returns:
        If value is decodeable by json.loads(), the decoded
        result is returned.  If the value is a legal utf-8 decodable
        string, it is returned as a string.  Otherwise, the value is
        returned as a bytes array.
    """
    try:
        return get_key_direct(flux_handle, key, namespace=namespace)
    except EnvironmentError as err:
        if err.errno == errno.EISDIR:
            pass
        else:
            raise err
    return get_dir(flux_handle, key, namespace=namespace)


def put(flux_handle, key, value):
    """Put data into the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: key to write to
        value: value of the key
    """
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    try:
        json_str = json.dumps(value)
        RAW.flux_kvs_txn_put(flux_handle.aux_txn, 0, key, json_str)
    except TypeError:
        if isinstance(value, bytes):
            RAW.flux_kvs_txn_put_raw(flux_handle.aux_txn, 0, key, value, len(value))
            return
        raise TypeError


def put_mkdir(flux_handle, key):
    """Create directory in the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: directory to create
    """
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    RAW.flux_kvs_txn_mkdir(flux_handle.aux_txn, 0, key)


def put_unlink(flux_handle, key):
    """Unlink key in the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: key to delete
    """
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    RAW.flux_kvs_txn_unlink(flux_handle.aux_txn, 0, key)


def put_symlink(flux_handle, key, target):
    """Create symlink in the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: symlink name
        target: target symlink points to
    """
    if flux_handle.aux_txn is None:
        flux_handle.aux_txn = RAW.flux_kvs_txn_create()
    RAW.flux_kvs_txn_symlink(flux_handle.aux_txn, 0, key, None, target)


def commit(flux_handle, flags: int = 0, namespace=None):
    """Commit changes to the KVS

    Must be called after put(), put_mkdir(), put_unlink(), or
    put_symlink() to write staged changes to the KVS.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        flags: defaults to 0, possible flag options:
          flux.constants.FLUX_KVS_NO_MERGE - disallow merging of different commits
          flux.constants.FLUX_KVS_TXN_COMPACT - if possible compact changes
          flux.constants.FLUX_KVS_SYNC - flush & checkpoint commit (only against primary KVS)
        namespace: namespace to write to, defaults to None.  If namespace
          is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.
    """
    if flux_handle.aux_txn is None:
        return
    future = RAW.flux_kvs_commit(flux_handle, namespace, flags, flux_handle.aux_txn)
    try:
        RAW.flux_future_get(future, None)
    except OSError:
        raise
    finally:
        RAW.flux_kvs_txn_destroy(flux_handle.aux_txn)
        flux_handle.aux_txn = None
        RAW.flux_future_destroy(future)


def namespace_create(flux_handle, namespace, owner=os.getuid(), flags: int = 0):
    """Create KVS Namespace

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        flags: defaults to 0, possible flag options:
          flux.constants.FLUX_KVS_NO_MERGE - disallow merging of different commits
          flux.constants.FLUX_KVS_TXN_COMPACT - if possible compact changes
          flux.constants.FLUX_KVS_SYNC - flush & checkpoint commit (only against primary KVS)
        namespace: namespace to create
        owner: uid of namespace owner, defaults to caller uid
        flags: currently unused, defaults to 0
    """
    future = RAW.flux_kvs_namespace_create(flux_handle, namespace, owner, flags)
    try:
        RAW.flux_future_get(future, None)
    finally:
        RAW.flux_future_destroy(future)


def namespace_remove(flux_handle, namespace):
    """Remove KVS Namespace

    Namespace is removed in background.  Caller cannot be certain of its removal
    after this function returns.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        namespace: namespace to remove
    """
    future = RAW.flux_kvs_namespace_remove(flux_handle, namespace)
    try:
        RAW.flux_future_get(future, None)
    finally:
        RAW.flux_future_destroy(future)


def namespace_list(flux_handle):
    """Get list of KVS Namespace

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()

    Returns:
        list: list of strings with names of namespaces
    """
    nslist = []
    rpc = RPC(flux_handle, "kvs.namespace-list")
    rsp = rpc.get()
    for ns in rsp["namespaces"]:
        nslist.append(ns["namespace"])
    return nslist


def dropcache(flux_handle):
    """Drop KVS cache entries

    Inform KVS module to drop cache entries without a reference.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
    """
    RAW.flux_kvs_dropcache(flux_handle)


class KVSDir(WrapperPimpl, abc.MutableMapping):
    """User friendly class for KVS operations

    KVS values can be read or written through this class's item accessor.  e.g.

    mydir = KVSDir(flux_handle)
    print(mydir["mykey"])

    mydir["newkey"] = "foo"
    mydir.commit()

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        path: Optional base path for all read/write to be relative to (default ".")
        namespace: Optional namespace to read/write from/to, defaults to None.  If
          namespace is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.
    """

    # pylint: disable=too-many-ancestors, too-many-public-methods

    class InnerWrapper(Wrapper):

        # pylint: disable=no-value-for-parameter
        def __init__(self, flux_handle=None, path=".", handle=None, namespace=None):
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
                    flux_handle, namespace, RAW.FLUX_KVS_READDIR, path
                )
                RAW.flux_kvs_lookup_get_dir(future, directory)
                self.handle = RAW.flux_kvsdir_copy(directory[0])
                RAW.flux_future_destroy(future)
                if self.handle is None or self.handle == ffi.NULL:
                    raise EnvironmentError("No such file or directory")

    def __init__(self, flux_handle=None, path=".", handle=None, namespace=None):
        super(KVSDir, self).__init__()
        self.fhdl = flux_handle
        self.path = path
        # Helper var for easier concatenations
        if not path or path == ".":
            self._path = ""
        else:
            self._path = path if path[-1] == "." else path + "."
        self.namespace = namespace
        if flux_handle is None and handle is None:
            raise ValueError(
                "flux_handle must be a valid Flux object or"
                "handle must be a valid kvsdir cdata pointer"
            )
        self.pimpl = self.InnerWrapper(flux_handle, path, handle, namespace)

    def commit(self, flags=0):
        """Commit changes to the KVS

        When keys are added, removed, or updated in the KVSDir object, the
        changes are only cached in memory until the commit method asks the
        KVS service to make them permanent. The commit method only includes
        keys that have been explicitly updated in the KVSDir object, and the
        contents of the KVS directory may diverge from the contents of the
        KVSDir object if other changes are being made to the directory
        concurrently.

        After the commit method returns, updated keys can be accessed by other
        clients on the same broker rank. Other broker ranks are eventually
        consistent.
        """
        commit(self.fhdl, flags=flags, namespace=self.namespace)

    def key_at(self, key):
        """Get full path to KVS key"""
        p_str = self.pimpl.key_at(key)
        return p_str.decode("utf-8")

    def exists(self, name):
        """Evaluate if key exists in the basedir"""
        return exists(self.fhdl, self._path + name, namespace=self.namespace)

    def __getitem__(self, key):
        try:
            return get(self.fhdl, self.key_at(key), namespace=self.namespace)
        except EnvironmentError:
            raise KeyError(
                "{} not found under directory {}".format(key, self.key_at(""))
            )

    def __setitem__(self, key, value):
        put(self.fhdl, self._path + key, value)

    def __delitem__(self, key):
        put_unlink(self.fhdl, self._path + key)

    class KVSDirIterator(abc.Iterator):
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

    def fill(self, contents: Mapping[str, Any]):
        """Populate this directory with keys specified by contents

        Args:
            contents: A dict of keys and values to be created in the directory
              or None, sub-directories can be created by using ``dir.file``
              syntax, sub-dicts will be stored as json values in a single key

        """

        if contents is None:
            raise ValueError("contents must be non-None")

        try:
            for key, val in contents.items():
                self[key] = val
        finally:
            self.commit()

    def mkdir(self, key: str, contents: Mapping[str, Any] = None):
        """Create a new sub-directory, optionally pre-populated by
        contents, as would be done with ``fill(contents)``

        Args:
            key: Key of the directory to be created
            contents: A dict of keys and values to be created in the directory
              or None, sub-directories can be created by using `dir.file`
              syntax, sub-dicts will be stored as json values in a single key
        """

        put_mkdir(self.fhdl, self._path + key)
        self.commit()
        if contents is not None:
            new_kvsdir = KVSDir(self.fhdl, key, namespace=self.namespace)
            new_kvsdir.fill(contents)

    def files(self):
        """Get list of files in basedir"""
        for k in self:
            if not self.pimpl.isdir(k):
                yield k

    def directories(self):
        """Get list of directories in basedir"""
        for k in self:
            if self.pimpl.isdir(k):
                yield k

    def list_all(self):
        """Get tuple with list of files and directories in basedir"""
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
    """Convenience function for use with walk(), similar to os.path.join()"""
    return ".".join([a for a in args if len(a) > 0])


def _inner_walk(kvsdir, curr_dir, topdown=False, namespace=None):
    if topdown:
        yield (curr_dir, kvsdir.directories(), kvsdir.files())

    for directory in kvsdir.directories():
        path = join(curr_dir, directory)
        key = kvsdir.key_at(directory)
        for entry in _inner_walk(
            get_dir(kvsdir.fhdl, key, namespace=namespace),
            path,
            topdown,
            namespace=namespace,
        ):
            yield entry

    if not topdown:
        yield (curr_dir, kvsdir.directories(), kvsdir.files())


def walk(directory, topdown=False, flux_handle=None, namespace=None):
    """Walk a directory in the style of os.walk()

    Args:
        directory: A path or KVSDir object
        topdown: Specify True for current directory to be
          listed before subdirectories.
        flux_handle: Required if "directory" is a path.
        namespace: namespace to read from, defaults to None.  If namespace
          is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.
    """
    if not isinstance(directory, KVSDir):
        if flux_handle is None:
            raise ValueError("If directory is a key, flux_handle must be specified")
        directory = KVSDir(flux_handle, directory, namespace=namespace)
    return _inner_walk(directory, "", topdown, namespace=namespace)
