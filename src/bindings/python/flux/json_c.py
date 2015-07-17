from _core import ffi, lib

class Jobj(object):
  def __init__(self, json_str=''):
    self.json_obj = ffi.new('struct json_object *[1]')
    self.json_obj[0] = lib.json_tokener_parse(json_str)

  def __del__(self):
    lib.json_object_put(self.json_obj[0])

  def get(self):
    return self.json_obj[0]

  def get_as_dptr(self):
    return self.json_obj

  def as_str(self):
    if self.json_obj[0] == ffi.NULL:
      return ''
    else:
      return ffi.string(
          lib.json_object_to_json_string(self.json_obj[0])
          )
  def __str__(self):
    return self.as_str()

