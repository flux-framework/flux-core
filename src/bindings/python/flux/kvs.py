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
from abc import ABC, abstractmethod
from typing import Any, Mapping

import flux.constants
from _flux._core import ffi, lib
from flux.future import Future
from flux.rpc import RPC
from flux.wrapper import Wrapper, WrapperPimpl


class KVSWrapper(Wrapper):
    # This empty class accepts new methods, preventing accidental overloading
    # across wrappers
    pass


RAW = KVSWrapper(ffi, lib, prefixes=["flux_kvs", "flux_kvs_"])
# override error check behavior for flux_kvsitr_next
RAW.flux_kvsitr_next.set_error_check(lambda x: False)


def _get_value(valp):
    try:
        ret = json.loads(ffi.string(valp[0]).decode("utf-8"))
    except json.decoder.JSONDecodeError:
        ret = ffi.string(valp[0]).decode("utf-8")
    except UnicodeDecodeError:
        ret = ffi.string(valp[0])
    return ret


def get_key_direct(flux_handle, key, namespace=None):
    valp = ffi.new("char *[1]")
    future = RAW.flux_kvs_lookup(flux_handle, namespace, 0, key)
    RAW.flux_kvs_lookup_get(future, valp)
    if valp[0] == ffi.NULL:
        return None

    ret = _get_value(valp)
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


def get_dir(flux_handle, key=".", namespace=None, _kvstxn=None):
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
    return KVSDir(
        path=key, flux_handle=flux_handle, namespace=namespace, _kvstxn=_kvstxn
    )


def get(flux_handle, key, namespace=None, _kvstxn=None):
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
    return get_dir(flux_handle, key, namespace=namespace, _kvstxn=_kvstxn)


# convenience function to get RAW kvs txn object to use
def _get_kvstxn(flux_handle, _kvstxn):
    # If _kvstxn is None, use the default txn stored in the flux
    # handle (create it if necessary)
    if _kvstxn is None:
        if flux_handle.aux_txn is None:
            flux_handle.aux_txn = RAW.flux_kvs_txn_create()
        _kvstxn = flux_handle.aux_txn
    elif isinstance(_kvstxn, KVSTxn):
        # if _kvstxn is type KVSTxn, get the RAW kvs txn
        # stored within it
        _kvstxn = _kvstxn.txn
    # we don't perform this check, as it would require an
    # import of _cffi_backend
    # elif not isinstance(_kvstxn, _cffi_backend.FFI.CData):
    #     raise TypeError
    return _kvstxn


def put(flux_handle, key, value, _kvstxn=None):
    """Put data into the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: key to write to
        value: value of the key
    """
    _kvstxn = _get_kvstxn(flux_handle, _kvstxn)
    try:
        json_str = json.dumps(value)
        RAW.flux_kvs_txn_put(_kvstxn, 0, key, json_str)
    except TypeError:
        if isinstance(value, bytes):
            RAW.flux_kvs_txn_put_raw(_kvstxn, 0, key, value, len(value))
            return
        raise TypeError


def put_mkdir(flux_handle, key, _kvstxn=None):
    """Create directory in the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: directory to create
    """
    _kvstxn = _get_kvstxn(flux_handle, _kvstxn)
    RAW.flux_kvs_txn_mkdir(_kvstxn, 0, key)


def put_unlink(flux_handle, key, _kvstxn=None):
    """Unlink key in the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: key to delete
    """
    _kvstxn = _get_kvstxn(flux_handle, _kvstxn)
    RAW.flux_kvs_txn_unlink(_kvstxn, 0, key)


def put_symlink(flux_handle, key, target, _kvstxn=None):
    """Create symlink in the KVS

    Internally will stage changes until commit() is called.

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: symlink name
        target: target symlink points to
    """
    _kvstxn = _get_kvstxn(flux_handle, _kvstxn)
    RAW.flux_kvs_txn_symlink(_kvstxn, 0, key, None, target)


def commit(flux_handle, flags: int = 0, namespace=None, _kvstxn=None):
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
    if _kvstxn is None:
        # If _kvstxn is None, use the default txn stored in the flux
        # handle.  If aux_txn is None, there is nothing to commit.
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
    else:
        # if _kvstxn is type KVSTxn, get the RAW kvs txn stored within
        # it.  If not type KVSTxn, it is assumed to of type RAW txn.
        if isinstance(_kvstxn, KVSTxn):
            _kvstxn = _kvstxn.txn
        future = RAW.flux_kvs_commit(flux_handle, namespace, flags, _kvstxn)
        try:
            RAW.flux_future_get(future, None)
        except OSError:
            raise
        finally:
            RAW.flux_future_destroy(future)


def commit_async(flux_handle, flags: int = 0, namespace=None, _kvstxn=None):
    """Commit changes to the KVS.  Identical to commit(), but returns
    a Future to wait on for RPC to complete.

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

    Returns:
        Future: a future fulfilled when the commit RPC returns
    """
    if _kvstxn is None:
        # If _kvstxn is None, use the default txn stored in the flux
        # handle.  If aux_txn is None, make an empty txn for the
        # commit.
        if flux_handle.aux_txn is None:
            flux_handle.aux_txn = RAW.flux_kvs_txn_create()
        future = RAW.flux_kvs_commit(flux_handle, namespace, flags, flux_handle.aux_txn)
        RAW.flux_kvs_txn_destroy(flux_handle.aux_txn)
        flux_handle.aux_txn = None
        return Future(future)
    else:
        # if _kvstxn is type KVSTxn, get the RAW kvs txn stored within
        # it.  If not type KVSTxn, it is assumed to of type RAW txn.
        if isinstance(_kvstxn, KVSTxn):
            _kvstxn = _kvstxn.txn
        future = RAW.flux_kvs_commit(flux_handle, namespace, flags, _kvstxn)
        return Future(future)


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


class KVSTxn:
    """KVS Transaction Object

    Stage changes to the KVS.  When all changes have been placed
    within the transaction, use commit() to finalize the transaction.
    Can be used as a context manager and commits will be handled at
    exit. e.g.

    with KVSTxn(handle, "basedirectory") as kt:
        kt.put("a", 1)

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        path: Optional base path for all writes to be relative to (default ".")
        namespace: Optional namespace to write to, defaults to None.  If
          namespace is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.
    """

    def __init__(self, flux_handle=None, path=".", namespace=None):
        self.fhdl = flux_handle
        self.path = path
        # Helper var for easier concatenations
        if not path or path == ".":
            self._path = ""
        else:
            self._path = path if path[-1] == "." else path + "."
        self.namespace = namespace
        self.txn = RAW.flux_kvs_txn_create()

    def commit(self, flags=0):
        """Commit changes to the KVS

        When keys are added, removed, or updated in the KVSTxn object, the
        changes are only cached in memory until the commit method asks the
        KVS service to make them permanent.

        After the commit method returns, updated keys can be accessed by other
        clients on the same broker rank. Other broker ranks are eventually
        consistent.
        """
        # If no transactions stored, no need to waste an RPC on a commit call
        if RAW.flux_kvs_txn_is_empty(self.txn):
            return
        try:
            commit(self.fhdl, flags=flags, namespace=self.namespace, _kvstxn=self.txn)
        except OSError:
            raise
        finally:
            self.clear()

    def commit_async(self, flags=0):
        """Commit changes to the KVS.  Identical to commit(), but returns a
        Future to wait on for RPC to complete.
        """
        future = commit_async(
            self.fhdl, flags=flags, namespace=self.namespace, _kvstxn=self.txn
        )
        self.clear()
        return Future(future)

    def put(self, key, value):
        """Put key=value in the KVS"""
        put(self.fhdl, self._path + key, value, _kvstxn=self.txn)

    def mkdir(self, key):
        """Create a directory in the KVS"""
        put_mkdir(self.fhdl, self._path + key, _kvstxn=self.txn)

    def unlink(self, key):
        """Unlink key in the KVS"""
        put_unlink(self.fhdl, self._path + key, _kvstxn=self.txn)

    def symlink(self, key, target):
        """Create a symlink in the KVS"""
        put_symlink(self.fhdl, self._path + key, target, _kvstxn=self.txn)

    def clear(self):
        RAW.flux_kvs_txn_clear(self.txn)

    def __del__(self):
        RAW.flux_kvs_txn_destroy(self.txn)

    def __enter__(self):
        """Allow this to be used as a context manager"""
        return self

    def __exit__(self, type_arg, value, tb):
        """
        When used as a context manager, the KVSTxn commits itself on exit
        """
        self.commit()
        return False


class KVSDir(WrapperPimpl, abc.MutableMapping):
    """User friendly class for KVS operations

    KVS values can be read or written through this class's item accessor.  e.g.

    mydir = KVSDir(flux_handle)
    print(mydir["mykey"])

    mydir["newkey"] = "foo"
    mydir.commit()

    Any KVS directories accessed through the item accessor will share
    the same internal KVS transaction, so that only a single call to
    commit() is necessary.  e.g.

    mydir = KVSDir(flux_handle)
    subdir = mydir["subdir"]
    subdir["anotherkey"] = "bar"
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

    def __init__(
        self, flux_handle=None, path=".", handle=None, namespace=None, _kvstxn=None
    ):
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
        # See comment in __getitem__ as to why we don't set path to "." and not self.path
        self.kvstxn = (
            _kvstxn if _kvstxn else KVSTxn(self.fhdl, ".", namespace=self.namespace)
        )

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
        try:
            self.kvstxn.commit(flags=flags)
        except OSError:
            raise
        finally:
            self.kvstxn.clear()

    def commit_async(self, flags=0):
        """Commit changes to the KVS.  Identical to commit(), but returns a
        Future to wait on for RPC to complete.
        """
        future = self.kvstxn.commit_async(flags=flags)
        self.kvstxn.clear()
        return Future(future)

    def key_at(self, key):
        """Get full path to KVS key"""
        p_str = self.pimpl.key_at(key)
        return p_str.decode("utf-8")

    def exists(self, name):
        """Evaluate if key exists in the basedir"""
        return exists(self.fhdl, self._path + name, namespace=self.namespace)

    def __getitem__(self, key):
        # it is common for users to do something like
        #
        # with flux.kvs.get_dir(self.f) as kd:
        #    kd["dir"]["subdir"]["subsubdir"]["a"] = 1
        #
        # the ability to get KVSDir subdirectories via the item
        # accessor requires us to share this KVS transaction with
        # subdirs for the final commit.  So if the user gets a KVSDir
        # via item accessor, give it the same KVS transaction object
        # by passing self.txn to get()..
        #
        # This also requires all updates to the KVSTxn object to be based
        # on an initial path of "." in the transaction object.  i.e. use
        # the "absolute path".  Otherwise, subdirs may write to the wrong
        # relative location.
        try:
            val = get(
                self.fhdl,
                self.key_at(key),
                namespace=self.namespace,
                _kvstxn=self.kvstxn,
            )
        except EnvironmentError:
            raise KeyError(
                "{} not found under directory {}".format(key, self.key_at(""))
            )
        return val

    def __setitem__(self, key, value):
        # See note in __getitem__, we always write to "absolute" path
        self.kvstxn.put(self._path + key, value)

    def __delitem__(self, key):
        # See note in __getitem__, we always write to "absolute" path
        self.kvstxn.unlink(self._path + key)

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

        # See note in __getitem__, we always write to "absolute" path
        self.kvstxn.mkdir(self._path + key)
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


class WatchImplementation(Future, ABC):
    """
    Interface for KVS based watchers

    Users to implement watch_get() and watch_cancel() functions.
    """

    def __del__(self):
        if self.needs_cancel is not False:
            self.cancel()
        try:
            super().__del__()
        except AttributeError:
            # not an error if super did not implement
            pass

    def __init__(self, future_handle):
        super().__init__(future_handle)
        self.needs_cancel = True

    @abstractmethod
    def watch_get(self, future):
        pass

    @abstractmethod
    def watch_cancel(self, future):
        pass

    def get(self, autoreset=True):
        """
        Return the new value or None if the stream has terminated.

        The future is auto-reset unless autoreset=False, so a subsequent
        call to get() will try to fetch the next value and thus
        may block.
        """
        try:
            #  Block until Future is ready:
            self.wait_for()
            ret = self.watch_get(self.pimpl)
        except OSError as exc:
            if exc.errno == errno.ENODATA:
                self.needs_cancel = False
                return None
            # raise handle exception if there is one
            self.raise_if_handle_exception()
            # re-raise all other exceptions
            #
            # Note: overwrite generic OSError strerror string with the
            # EventWatch future error string to give the caller appropriate
            # detail (e.g. instead of "No such file or directory" use
            # "job <jobid> does not exist"
            #
            exc.strerror = self.error_string()
            raise
        if autoreset is True:
            self.reset()
        return ret

    def cancel(self, stop=False):
        """Cancel a streaming future

        If stop=True, then deactivate the multi-response future so no
        further callbacks are called.
        """
        self.watch_cancel(self.pimpl)
        self.needs_cancel = False
        if stop:
            self.stop()


class KVSWatchFuture(WatchImplementation):
    """
    A future returned from kvs_watch_async().
    """

    def __init__(self, future_handle):
        super().__init__(future_handle)

    def watch_get(self, future):
        """
        Implementation of watch_get() for KVSWatchFuture.

        Will be called from WatchABC.get()
        """
        valp = ffi.new("char *[1]")
        RAW.flux_kvs_lookup_get(future, valp)
        return _get_value(valp)

    def watch_cancel(self, future):
        """
        Implementation of watch_cancel() for KVSWatchFuture.

        Will be called from WatchABC.cancel()
        """
        RAW.flux_kvs_lookup_cancel(future)


def kvs_watch_async(
    flux_handle, key, namespace=None, waitcreate=False, uniq=False, full=False
):
    """Asynchronously get KVS updates for a key

    Args:
        flux_handle: A Flux handle obtained from flux.Flux()
        key: the key on which to watch
        namespace: namespace to read from, defaults to None.  If namespace
          is None, the namespace specified in the FLUX_KVS_NAMESPACE
          environment variable will be used.  If FLUX_KVS_NAMESPACE is not
          set, the primary namespace will be used.
        waitcreate: If True and a key does not yet exist, will wait
          for it to exit.  Defaults to False.
        uniq: If True, only different values will be returned by
          watch.  Defaults to False.
        full: If True, any change that can affect the key is
          monitored.  Typically, this is to capture when a parent directory
          is removed or altered in some way.  Typically kvs watch will not
          detect this as the exact key has not been changed.  Defaults to
          False.

    Returns:
        KVSWatchFuture: a KVSWatchFuture object.  Call .get() from the then
         callback to get the currently returned value from the Future object.
    """

    flags = flux.constants.FLUX_KVS_WATCH
    if waitcreate:
        flags |= flux.constants.FLUX_KVS_WAITCREATE
    if uniq:
        flags |= flux.constants.FLUX_KVS_WATCH_UNIQ
    if full:
        flags |= flux.constants.FLUX_KVS_WATCH_FULL
    future = RAW.flux_kvs_lookup(flux_handle, namespace, flags, key)
    return KVSWatchFuture(future)
