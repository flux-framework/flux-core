/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <sys/resource.h>
#include <json.h>

#include "log.h"
#include "shortjson.h"
#include "getrusage_json.h"

int getrusage_json (int who, json_object **op)
{
    struct rusage ru;
    json_object *o;

    if (getrusage (who, &ru) < 0)
        return -1;
    o = Jnew ();
    Jadd_double (o, "utime",
            (double)ru.ru_utime.tv_sec + 1E-6 * ru.ru_utime.tv_usec);
    Jadd_double (o, "stime",
            (double)ru.ru_stime.tv_sec + 1E-6 * ru.ru_stime.tv_usec);
    Jadd_int64 (o, "maxrss",        ru.ru_maxrss);
    Jadd_int64 (o, "ixrss",         ru.ru_ixrss);
    Jadd_int64 (o, "idrss",         ru.ru_idrss);
    Jadd_int64 (o, "isrss",         ru.ru_isrss);
    Jadd_int64 (o, "minflt",        ru.ru_minflt);
    Jadd_int64 (o, "majflt",        ru.ru_majflt);
    Jadd_int64 (o, "nswap",         ru.ru_nswap);
    Jadd_int64 (o, "inblock",       ru.ru_inblock);
    Jadd_int64 (o, "oublock",       ru.ru_oublock);
    Jadd_int64 (o, "msgsnd",        ru.ru_msgsnd);
    Jadd_int64 (o, "msgrcv",        ru.ru_msgrcv);
    Jadd_int64 (o, "nsignals",      ru.ru_nsignals);
    Jadd_int64 (o, "nvcsw",         ru.ru_nvcsw);
    Jadd_int64 (o, "nivcsw",        ru.ru_nivcsw);
    *op = o;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
