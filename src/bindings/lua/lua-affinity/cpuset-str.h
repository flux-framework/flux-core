/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 *
 *  This file is part of slurm-spank-plugins, a set of spank plugins for SLURM.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include <sched.h>

#ifndef _HAVE_CPUSET_STR_H
#define _HAVE_CPUSET_STR_H

int hex_to_cpuset (cpu_set_t *mask, const char *str);
int cpuset_to_hex (cpu_set_t *mask, char *str, size_t len, char *sep);
int cstr_to_cpuset(cpu_set_t *mask, const char* str);
int str_to_cpuset(cpu_set_t *mask, const char* str);
char * cpuset_to_cstr (cpu_set_t *mask, char *str);

#endif /* !_HAVE_CPUSET_STR_H */
