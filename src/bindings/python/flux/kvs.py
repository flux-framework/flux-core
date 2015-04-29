from _kvs import ffi, lib
import flux
from flux.wrapper import Wrapper, WrapperPimpl, WrapperBase
import flux.json_c as json_c
import json
import collections
import contextlib
import errno

raw = Wrapper(ffi, lib, prefixes=['kvs','kvs_'])
 

class KVSHandle(Wrapper):
  def __init__(self, flux_handle=None):
    if flux_handle is None:
      raise ValueError("flux_handle must be a valid Flux object")
    super(self.__class__, self).__init__(ffi, lib, handle=flux_handle,
                                     match=ffi.typeof(lib.flux_open).result,
                                     prefixes=[
                                       'kvs_',
                                       ],
                                     )

class KVSDir(WrapperPimpl,collections.MutableMapping):
  class InnerWrapper(Wrapper):
    def __init__(self, flux_handle=None, path='.', handle=None):
      self.handle = None
      global raw
      if flux_handle is None and handle is None:
        raise ValueError("flux_handle must be a valid Flux object or handle must be a valid kvsdir cdata pointer")
      if isinstance(flux_handle, WrapperBase):
        flux_handle = flux_handle.handle
      if handle is None:
        d = ffi.new("kvsdir_t [1]")
        raw.kvs_get_dir(flux_handle, d, path)
        handle = d[0]
        if handle is None or handle == ffi.NULL:
          raise IOError(errno.ENOENT, os.strerror(errno.ENOENT))

      super(self.__class__, self).__init__(ffi, lib, handle=handle,
                                       match=ffi.typeof('kvsdir_t'),
                                       prefixes=[
                                         'kvsdir_',
                                         ],
                                       )
    def __del__(self):
      if self.handle is not None:
        raw.kvsdir_destroy(self.handle)

  def __init__(self, flux_handle=None, path='.', handle=None):
      self.fh = flux_handle
      self.path = path
      if flux_handle is None and handle is None:
        raise ValueError("flux_handle must be a valid Flux object or handle must be a valid kvsdir cdata pointer")
      self.pimpl = self.InnerWrapper(flux_handle, path, handle)

  def commit(self):
    global raw
    ret = raw.kvs_commit(self.fh.handle)
    if ret < 0:
      raise EnvironmentError(ret)

  def key_at(self, key):
    c_str = self.pimpl.key_at(key)
    p_str = ffi.string(c_str)
    lib.free(c_str)
    return p_str

  def __getitem__(self, key):
    try:
      j = json_c.Jobj()
      self.pimpl.get(key, j.get_as_dptr())
      return json.loads(j.as_str())
    except EnvironmentError as err:
      if err.errno == errno.EISDIR:
        pass
      else:
        raise KeyError(key)

    nd = ffi.new('kvsdir_t [1]')
    self.pimpl.get_dir(nd, key)
    return KVSDir(flux_handle=self.fh, handle = nd[0])

  
  def __setitem__(self, key, value):
    ret = -1
    if value is None:
      ret = self.pimpl.put(key, ffi.NULL)
    elif isinstance(value, int):
      ret = self.pimpl.put_int64(key, value)
    elif isinstance(value, float):
      ret = self.pimpl.put_double(key, value)
    elif isinstance(value, bool):
      ret = self.pimpl.put_boolean(key, value)
    else:
      # Turn it into json
      j = json_c.Jobj(json.dumps(value))
      ret = self.pimpl.put(key, j.get())
      # print "putting:", j.as_str(), ret
    if ret < 0:
      raise ValueError()


  def __delitem__(self, key):
    self.pimpl.unlink(key)

  class KVSDirIterator(Wrapper, collections.Iterator):
    def __init__(self, kd):
      self.kd = kd
      self.itr = None
      self.itr = raw.kvsitr_create(kd.handle)

    def __del__(self):
      raw.kvsitr_destroy(self.itr)

    def __iter__(self):
      return self

    def __next__(self):
      ret = raw.kvsitr_next(self.itr)
      if ret is None or ret == ffi.NULL:
        raise StopIteration()
      return ffi.string(ret)

    def next(self):
      return self.__next__()

  def __iter__(self):
    return self.KVSDirIterator(self)

  def __len__(self):
    # TODO find a less horrifyingly inefficient way to do this
    count = 0
    for b in self:
      count += 1
    return count

  def fill(self, contents):
    """ Populate this directory with keys specified by contents, which must
    conform to the Mapping interface

    :param contents: A dict of keys and values to be created in the directory
      or None, sub-directories can be created by using `dir.file` syntax,
      sub-dicts will be stored as json values in a single key """

    if contents is None:
      raise ValueError("contents must be non-None")

    with commit_guard(self) as kd:
      for k, v in contents.items():
        self[k] = v


  def mkdir(self, key, contents=None):
    """ Create a new sub-directory, optionally pre-populated with the contents
    of `files` as would be done with `fill(contents)`

    :param key: Key of the directory to be created
    :param contents: A dict of keys and values to be created in the directory
      or None, sub-directories can be created by using `dir.file` syntax,
      sub-dicts will be stored as json values in a single key """

    self.pimpl.mkdir(key)
    new_kvsdir = KVSDir(self.fh, key)
    if contents is not None:
      new_kvsdir.fill(contents)

  def list_all(self, topdown=False):
    files = []
    dirs = []
    for k in self.keys():
      if self.pimpl.isdir(k):
        dirs.append(k)
      else:
        files.append(k)
    return (files, dirs)


def walk(kd, topdown=False):
  """ Walk a directory in the style of os.walk() """
  key = kd.key_at('')
  (files, dirs) = kd.list_all(topdown)
  if topdown:
    yield (key, dirs, files)

  for d in dirs:
    for x in walk(kd[d], topdown):
      yield x

  if not topdown:
    yield (key, dirs, files)






@ffi.callback('KVSSetF')
def KVSWatchWrapper(key, value, arg, errnum):
  j = Jobj(handle = value)
  (cb, real_arg) = ffi.from_handle(arg)
  return cb(key, json.loads(j.as_str()), real_arg, errnum)

kvswatches = {}
def watch(flux_handle, key, fun, arg):
  warg = (fun, arg)
  kvswatches[key] = warg
  return raw.watch(flux_handle, key, KVSWatchWrapper, ffi.new_handle(warg))

def unwatch(flux_handle, key):
  kvswatches.pop(key, None)
  return raw.unwatch(flux_handle, key)





@contextlib.contextmanager
def get_dir(flux_handle, path='.'):
  kd = KVSDir(flux_handle, path)
  try:
    yield kd
  finally:
    kd.commit()

@contextlib.contextmanager
def commit_guard(obj):
  try:
    yield obj
  finally:
    obj.commit()

@contextlib.contextmanager
def fence_guard(flux_handle, name='', nprocs=1):
  try:
    yield None
  finally:
    raw.kvs_fence(flux_handle, name, nprocs)





