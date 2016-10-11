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

#include <Python.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/liboptparse/optparse.h"
#include "src/common/libutil/xzmalloc.h"

#ifndef Py_PYTHON_H
typedef void PyObject;
#endif

void add_if_not_present(PyObject *list, const char* path){
    if(path){
        PyObject *pymod_path = PyString_FromString(path);
        if (!PySequence_Contains(list, pymod_path)){
            PyList_Append(list, pymod_path);
        }else{
            Py_DECREF(pymod_path);
        }
    }
}

void print_usage(){
    printf("pymod usage: flux module load pymod --module=<modname> [--path=<module path>] [--verbose] [--help]]\n");
}

zhash_t *zhash_fromargv (int argc, char **argv)
{
    zhash_t *args = zhash_new ();
    int i;

    if (args) {
        for (i = 0; i < argc; i++) {
            char *key = xstrdup (argv[i]);
            char *val = strchr (key, '=');
            if (val) {
                *val++ = '\0';
                zhash_update (args, key, xstrdup (val));
                zhash_freefn (args, key, free);
            }
            free (key);
        }
    }
    return args;
}

const char *usage_msg = "[OPTIONS] MODULE_NAME";
static struct optparse_option opts[] = {
    { .name = "verbose",    .key = 'v', .has_arg = 0,
      .usage = "Be loud", },
    { .name = "path",     .key = 'p', .has_arg = 1, .arginfo = "PATH",
      .usage = "Director{y,ies} to add to PYTHONPATH before finding your module", },
    OPTPARSE_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    optparse_t *p = optparse_create ("pymod");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if (optparse_set (p, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set usage");
    int option_index = optparse_parse_args (p, argc, argv);

    if (option_index <= 0 || optparse_hasopt(p, "help") || option_index >= argc){
        optparse_print_usage(p);
        return (option_index < 0);
    }
    const char * module_name = argv[option_index];

    Py_SetProgramName("pymod");
    Py_Initialize();

    PyObject *search_path = PySys_GetObject("path");
    // Add installation search paths
    add_if_not_present(search_path, optparse_get_str(p, "path", ""));
    add_if_not_present(search_path, FLUX_PYTHON_PATH);

    PySys_SetObject("path", search_path);
    if(optparse_hasopt(p, "verbose")){
        PyObject_Print(search_path, stderr, 0);
    }

    flux_log(h, LOG_INFO, "loading python module named: %s", module_name);

    PyObject *module = PyImport_ImportModule("flux.core");
    if(!module){
        PyErr_Print();
        return EINVAL;
    }

    PyObject *mod_main = PyObject_GetAttrString(module, "mod_main_trampoline");
    if(mod_main && PyCallable_Check(mod_main)){
        //maybe unpack args directly? probably easier to use a dict
        PyObject *py_args = PyTuple_New(3);
        PyTuple_SetItem(py_args, 0, PyString_FromString(module_name));
        PyTuple_SetItem(py_args, 1, PyLong_FromVoidPtr(h));

        //Convert zhash to native python dict, should preserve mods
        //through switch to argc-style arguments
        PyObject *arg_list = PyList_New(0);
        char ** it = argv + option_index;
        int i;
        for (i=0; *it; i++, it++){
            PyList_Append(arg_list, PyString_FromString(*it));
        }

        PyTuple_SetItem(py_args, 2, arg_list);
        // Call into trampoline
        PyObject_CallObject(mod_main, py_args);
        if(PyErr_Occurred()){
            PyErr_Print();
        }
        Py_DECREF(py_args);
        Py_DECREF(arg_list);
    }
    Py_Finalize();
    return 0;
}

MOD_NAME ("pymod");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
