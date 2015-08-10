import sys
import os
import re
import subprocess

import argparse
parser = argparse.ArgumentParser()

parser.add_argument('header', help='C header file to parse', type=str)
parser.add_argument('--include_header', help='Include base path', type=str, default='')
parser.add_argument('--include_ffi', help='FFI module for inclusion', action='append', default=[]) 
parser.add_argument('--package', help='Package prefix for module import', default=None)
parser.add_argument('--path', help='Include base path', default='.')
parser.add_argument('--lib_path', help='Library base path', default='.')
parser.add_argument('--modname', help='Name for the module to be generated', default='_flux')
parser.add_argument('--library', help='Library to include in the build', default='flux-core')
parser.add_argument('--add_long_sub', help='Regex filter to apply whole-file of the form <match>|||<replacement>', action='append', default=[])
parser.add_argument('--add_sub', '-a', help='Regex filter to apply in processing of the form <match>|||<replacement>', action='append', default=[])
parser.add_argument('--extra_source', help='Source to add directly to the output, mainly for includes', type=str, default='')
parser.add_argument('--ignore_header', help='Pattern to ignore when searching header files', default=[], action='append')

args = parser.parse_args()

absolute_head = os.path.abspath(os.path.join(args.path, args.header))

def process_header(f):
  global mega_header
  if f not in checked_heads:
    for p in args.ignore_header:
      if re.search(p, f):
        checked_heads[f] = 1
        return
    with open(f, 'r') as header:
      s = header.read()
      s = re.sub(r'\\\n', '', s)
      s = re.sub(r'\/\*([\s\S]*?)\*\/', '', s)
      s = re.sub(r'__attribute__\s*(([^;]*))', '', s)

      for sub in args.add_long_sub:
        [m, r] = sub.split('|||')
        s = re.sub(m, r, s)

      lines = s.split('\n')

      for sub in args.add_sub:
        new_lines = []
        [m, r] = sub.split('|||')
        for l in lines:
          l = re.sub(m, r, l)
          new_lines.append(l)
        lines = new_lines

      for l in lines:
        m = re.search('#include\s*"([^"]*)"', l)
        if m:
          process_header(m.group(1))
        if not re.match("#\s*(ifdef|ifndef|endif|include|define)", l):
          mega_header += l+'\n'
    checked_heads[f] = 1


with open('{}_build.py'.format(args.modname), 'w') as modfile:
  os.chdir(args.path)

  mega_header = ''
  checked_heads = {}

  process_header(absolute_head)

  include_head = args.header
  if args.include_header != '':
    include_head = args.include_header

  mega_header = """
  """ + mega_header

  ffi_include_base = '''
from {module} import ffi as {module}_ffi
ffi.include({module}_ffi)
  '''
  ffi_include = ''
  for inc in args.include_ffi:
    ffi_include += ffi_include_base.format(module=inc)




  print >>modfile, '''
from cffi import FFI
ffi = FFI()


ffi.set_source('{full_mod}',
            """
#include <{header}>
{extra_source}

//TODO: REMOVE THIS when the json_obj stuff goes away
#include <json.h>

void * unpack_long(ptrdiff_t num){{
  return (void*)num;
}}
            """,
            libraries=['{library}'])

{includes}

ffi.cdef("""
void * unpack_long(ptrdiff_t num);
void free(void *ptr);

struct json_object;
extern typedef struct json_object json_object;
struct json_object* json_tokener_parse(const char *str);
int json_object_put(struct json_object *obj);
const char *  json_object_to_json_string (struct json_object *obj);

        {cdefs}


    """)
if __name__ == "__main__":
  ffi.emit_c_code('{modname}.c')
    '''.format(
        cdefs=mega_header,
        full_mod=args.modname if args.package is None else '.'.join([args.package,args.modname]),
        modname=args.modname,
        library=args.library,
        header=include_head,
        extra_source=args.extra_source,
        includes=ffi_include
        )
