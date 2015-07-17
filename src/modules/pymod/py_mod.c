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

int mod_main (flux_t h, zhash_t *args_in)
{
    zhash_t *args = zhash_dup(args_in);
    Py_SetProgramName("pymod");
    Py_Initialize();
    /* flux_log(h, LOG_INFO, "in pymod mod_main"); */

    if(zhash_lookup(args, "--help")){
        print_usage();
        return 0;
    }

    PyObject *search_path = PySys_GetObject("path");
    _Bool verbose = (zhash_lookup(args, "--path") != NULL);

    // Add installation search paths
    char * module_path = zhash_lookup(args, "--path");
    add_if_not_present(search_path, module_path);
    add_if_not_present(search_path, FLUX_PYTHON_PATH);

    PySys_SetObject("path", search_path);
    if(verbose){
        PyObject_Print(search_path, stderr, 0);
    }

    char * module_name = zhash_lookup(args, "--module");
    if(!module_name){
        print_usage();
        flux_log(h, LOG_ERR, "Module name must be specified with --module");
        return EINVAL;
    }
    flux_log(h, LOG_INFO, "loading python module named: %s\n", module_name);

    PyObject *module = PyImport_ImportModule("flux.core");
    if(!module){
        PyErr_Print();
        return EINVAL;
    }

    if(module){
        PyObject *mod_main = PyObject_GetAttrString(module, "mod_main_trampoline");
        if(mod_main && PyCallable_Check(mod_main)){
            //maybe unpack args directly? probably easier to use a dict
            PyObject *py_args = PyTuple_New(3);
            PyTuple_SetItem(py_args, 0, PyString_FromString(module_name));
            PyTuple_SetItem(py_args, 1, PyLong_FromVoidPtr(h));

            //Convert zhash to native python dict, should preserve mods
            //through switch to argc-style arguments
            PyObject *arg_dict = PyDict_New();
            void * value = zhash_first(args);
            const char * key = zhash_cursor(args);
            for(;value != NULL; value = zhash_next(args), key = zhash_cursor(args)){
                PyDict_SetItemString(arg_dict, key, PyString_FromString(value));
            }

            PyTuple_SetItem(py_args, 2, arg_dict);
            // Call into trampoline
            PyObject_CallObject(mod_main, py_args);
            if(PyErr_Occurred()){
                PyErr_Print();
            }
            Py_DECREF(py_args);
            Py_DECREF(arg_dict);
        }
    }
    zhash_delete(args, "--module");

    /* old test code, remove before pushing in */
    /* PyRun_SimpleString( "from time import time,ctime\n" */
    /*         "import sys\n" */
    /*         "print sys.path\n" */
    /*         "print 'Today is',ctime(time())\n"); */
    Py_Finalize();
    zhash_destroy(&args);
    return 0;
}

MOD_NAME ("pymod");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
