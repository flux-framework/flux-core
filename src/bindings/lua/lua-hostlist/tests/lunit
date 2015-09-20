#! /bin/sh

#   This file is part of lunit 0.5.
#
#   For Details about lunit look at: http://www.mroth.net/lunit/
#
#   Author: Michael Roth <mroth@nessie.de>
#
#   Copyright (c) 2004-2009 Michael Roth <mroth@nessie.de>
#
#   Permission is hereby granted, free of charge, to any person
#   obtaining a copy of this software and associated documentation
#   files (the "Software"), to deal in the Software without restriction,
#   including without limitation the rights to use, copy, modify, merge,
#   publish, distribute, sublicense, and/or sell copies of the Software,
#   and to permit persons to whom the Software is furnished to do so,
#   subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be
#   included in all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
#   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
#   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
#   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
#   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


if test $# = 0 ; then
  echo "$0: Usage Error. Try $0 --help" >&2
  exit 1
fi

if [ `uname` = "Darwin" ]; then 
  scriptname="$(readlink -n "$0")"
else
  scriptname="$(readlink -n -f "$0")"
fi
interpreter="lua"
options=""

while true ; do
  case "$1" in
    -h|--help)
      cat <<EOT
lunit 0.5
Copyright (c) 2004-2009 Michael Roth <mroth@nessie.de>
This program comes WITHOUT WARRANTY OF ANY KIND.

Usage: lunit [OPTIONS] [--] scripts

Options:

  -i, --interpreter LUA       Complete path of the lua binary to use.
  -p, --path PATH             Sets the LUA_PATH environment for the tests.
      --cpath CPATH           Sets the LUA_CPATH environment for the tests.
  -r, --runner RUNNER         Testrunner to use, defaults to 'lunit-console'.
  -t, --test PATTERN          Which tests to run, may contain * or ? wildcards.
      --loadonly              Only load the tests.
      --dontforce             Do not force to load $scriptname*.lua.
  -h, --help                  Print this help screen.
      --version               Print lunit version.

Please report bugs to <mroth@nessie.de>.
EOT
      exit ;;

    --version)
      echo "lunit 0.5 Copyright 2004-2009 Michael Roth <mroth@nessie.de>"
      exit ;;

    -i|--interpreter)
      interpreter="$2"
      shift 2 ;;

    -p|--path)
      LUA_PATH="$2"
      export LUA_PATH
      shift 2 ;;

    --cpath)
      LUA_CPATH="$2"
      export LUA_CPATH
      shift 2 ;;

    --loadonly)
      options="$options $1"
      shift 1 ;;

    --dontforce)
      scriptname=""
      shift 1 ;;

    -r|--runner|-t|--test)
      options="$options $1 $2"
      shift 2 ;;

    --)
      break ;;

    -*)
      echo "$0: Invalid option: $1" >&2
      exit 1 ;;

    *)
      break ;;
  esac
done


exec "$interpreter" - "$scriptname" $options "$@" <<EOT
  local scriptname = ...
  local argv = { select(2,...) }
  if scriptname ~= "" then
    local function force(name)
      pcall( function() loadfile(name)() end )
    end
    force( scriptname..".lua" )
    force( scriptname.."-console.lua" )
  end
  require "lunit"
  local stats = lunit.main(argv)
  if stats.errors > 0 or stats.failed > 0 then
    os.exit(1)
  end
EOT
