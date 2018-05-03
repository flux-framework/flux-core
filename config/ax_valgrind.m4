# ===========================================================================
#      https://github.com/flux-framework/flux-sched/
# ===========================================================================
# SYNOPSIS
#
#   AX_VALGRIND_H
#
# DESCRIPTION
#
#   Find valgrind.h first trying pkg-config, fallback to valgrind.h
#    or valgrind/valgrind.h
#
#  This macro will set
#  HAVE_VALGRIND if valgrind.h support was found
#  HAVE_VALGRIND_H if #include <valgrind.h> works
#  HAVE_VALGRIND_VALGRIND_H if #include <valgrind/valgrind.h> works
#
# LICENSE
#
#   Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
#   the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
#   LLNL-CODE-658032 All rights reserved.
#
#   This file is part of the Flux resource manager framework.
#   For details, see https://github.com/flux-framework.
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the Free
#   Software Foundation; either version 2 of the license, or (at your option)
#   any later version.
#
#   Flux is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc.,
#   59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
#   See also:  http://www.gnu.org/licenses/
#

AC_DEFUN([AX_VALGRIND_H], [
  PKG_CHECK_MODULES([VALGRIND], [valgrind],
      [AC_DEFINE([HAVE_VALGRIND], [1], [Define if you have valgrind.h])
       ax_valgrind_saved_CFLAGS=$CFLAGS
       ax_valgrind_saved_CPPFLAGS=$CPPFLAGS
       CFLAGS="$CFLAGS $VALGRIND_CFLAGS"
       CPPFLAGS="$CFLAGS"
       AC_CHECK_HEADERS([valgrind.h valgrind/valgrind.h])
       CFLAGS="$ax_valgrind_saved_CFLAGS"
       CPPFLAGS="$ax_valgrind_saved_CPPFLAGS"
      ],
      [AC_CHECK_HEADERS([valgrind.h valgrind/valgrind.h],
                        [AC_DEFINE([HAVE_VALGRIND], [1],
                                   [Define if you have valgrind.h])])
      ])
  ]
)
