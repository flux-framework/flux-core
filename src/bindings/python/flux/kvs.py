import json
import collections
import errno
from _flux._kvs import ffi, lib
from flux.wrapper import Wrapper, WrapperPimpl


class KVSWrapper(Wrapper):
    # This empty class accepts new methods, preventing accidental overloading
    # across wrappers
    pass

RAW = KVSWrapper(ffi, lib, prefixes=['flux_kvs', 'flux_kvs_'])
# override error check behavior for flux_kvsitr_next
RAW.flux_kvsitr_next.set_error_check(lambda x: False)


def get_key_direct(flux_handle, key):
    valp = ffi.new('char *[1]')
    RAW.flux_kvs_get(flux_handle, key, valp)
    if valp[0] == ffi.NULL:
        return None
    else:
        return json.loads(ffi.string(valp[0]))


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


def get_dir(flux_handle, key=''):
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
    RAW.flux_kvs_put(flux_handle, key, json_str)


def commit(flux_handle, flags=0):
    return RAW.flux_kvs_commit_anon(flux_handle, flags)


def dropcache(flux_handle):
    return RAW.flux_kvs_dropcache(flux_handle)


def watch_once(flux_handle, key):
    """
    Watches the selected key until the next change, then returns the
    updated value of the key
    """
    if isdir(flux_handle, key):
        directory = get_dir(flux_handle)
        # The wrapper automatically unpacks directory's handle
        RAW.flux_kvs_watch_once_dir(flux_handle, directory)
        return directory
    else:
        out_json_str = ffi.new('char *[1]')
        RAW.flux_kvs_watch_once(flux_handle, key, out_json_str)
        if out_json_str[0] == ffi.NULL:
            return None
        else:
            return json.loads(ffi.string(out_json_str[0]))


class KVSDir(WrapperPimpl, collections.MutableMapping):
    # pylint: disable=too-many-ancestors, too-many-public-methods

    class InnerWrapper(Wrapper):

        def __init__(self, flux_handle=None, path='.', handle=None):
            dest = RAW.flux_kvsdir_destroy
            super(self.__class__, self).__init__(ffi, lib,
                                                 handle=handle,
                                                 match=ffi.typeof(
                                                     'flux_kvsdir_t *'),
                                                 prefixes=[
                                                     'flux_kvsdir_',
                                                 ],
                                                 destructor=dest)

            if flux_handle is None and handle is None:  # pragma: no cover
                raise ValueError("flux_handle must be a valid Flux object or "
                                 "handle must be a valid kvsdir cdata pointer")
            if handle is None:
                directory = ffi.new("flux_kvsdir_t *[1]")
                RAW.flux_kvs_get_dir(flux_handle, directory, path)
                self.handle = directory[0]
                if self.handle is None or self.handle == ffi.NULL:
                    raise EnvironmentError("No such file or directory")

    def __init__(self, flux_handle=None, path='.', handle=None):
        super(KVSDir, self).__init__()
        self.fhdl = flux_handle
        self.path = path
        if flux_handle is None and handle is None:
            raise ValueError("flux_handle must be a valid Flux object or"
                             "handle must be a valid kvsdir cdata pointer")
        self.pimpl = self.InnerWrapper(flux_handle, path, handle)

    def commit(self, flags=0):
        commit(self.fhdl.handle, flags)

    def key_at(self, key):
        c_str = self.pimpl.key_at(key)
        p_str = ffi.string(c_str)
        lib.free(c_str)
        return p_str

    def exists(self, name):
        return self.pimpl.exists(name)

    def __getitem__(self, key):
        try:
            return get(self.fhdl, self.key_at(key))
        except EnvironmentError:
            raise KeyError(
                '{} not found under directory {}'.format(key, self.key_at('')))

    def __setitem__(self, key, value):
        # Turn it into json
        json_str = json.dumps(value)
        self.pimpl.put(key, json_str)

    def __delitem__(self, key):
        self.pimpl.unlink(key)

    class KVSDirIterator(collections.Iterator):

        def __init__(self, kvsdir):
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
            return ffi.string(ret)

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

        self.pimpl.mkdir(key)
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

    def watch_once(self, key):
        """
        Watches the selected key until the next change, then returns the
        updated value of the key
        """
        full_key = self.key_at(key)
        return watch_once(self.fhdl, full_key)


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
            raise ValueError(
                "If directory is a key, flux_handle must be specified")
        directory = KVSDir(flux_handle, directory)
    return inner_walk(directory, '', topdown)


@ffi.callback('kvs_set_f')
def kvs_watch_wrapper(key, value, arg, errnum):
    (callback, real_arg) = ffi.from_handle(arg)
    if errnum == errno.ENOENT:
        value = None
    else:
        value = json.loads(ffi.string(value))
    key = ffi.string(key)
    ret = callback(key, value, real_arg, errnum)
    return ret if ret is not None else 0


KVSWATCHES = {}


def watch(flux_handle, key, fun, arg):
    warg = (fun, arg)
    KVSWATCHES[key] = warg
    return RAW.flux_kvs_watch(flux_handle, key, kvs_watch_wrapper,
                              ffi.new_handle(warg))


def unwatch(flux_handle, key):
    KVSWATCHES.pop(key, None)
    return RAW.flux_kvs_unwatch(flux_handle, key)
