/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <Python.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <dlfcn.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/liboptparse/optparse.h"
#include "src/common/libutil/xzmalloc.h"

#ifndef Py_PYTHON_H
typedef void PyObject;
#endif

PyObject * py_unicode_or_string(const char *str) {
    return PyUnicode_FromString(str);
}

void add_if_not_present(PyObject *list, const char* path){
    if(path){
        PyObject *pymod_path = py_unicode_or_string(path);
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

static int register_pymod_service_name (flux_t *h, const char *name)
{
    flux_future_t *f;
    int saved_errno = 0;
    int rc = -1;

    /* Register a service name based on the name of the loaded script
     */
    if (!(f = flux_service_register (h, name))) {
        saved_errno = errno;
        flux_log_error (h, "service.add: flux_rpc_pack");
        goto done;
    }
    if ((rc = flux_future_get (f, NULL)) < 0) {
        saved_errno = errno;
        flux_log_error (h, "service.add: %s", name);
        goto done;
    }
done:
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

// Based on code from https://bugs.python.org/issue17870
PyObject* PyLong_FromUintptr_t(uintptr_t value)
{
    if (sizeof(uintptr_t) == sizeof(long)) {
        return PyLong_FromLong(value);
    } else if (sizeof(uintptr_t) <= sizeof(PY_LONG_LONG)) {
        return PyLong_FromLongLong((PY_LONG_LONG)value);
    } else {
        return NULL;
    }
}

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

    wchar_t *program = L"pymod";
    Py_SetProgramName(program);
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
    if (!dlopen (PYTHON_LIBRARY, RTLD_LAZY|RTLD_GLOBAL))
        flux_log_error (h, "Unable to dlopen libpython");

    PyObject *module = PyImport_ImportModule("flux.core.trampoline");
    if(!module){
        PyErr_Print();
        return EINVAL;
    }
    if (register_pymod_service_name (h, module_name) < 0)
        return -1;

    PyObject *mod_main = PyObject_GetAttrString(module, "mod_main_trampoline");
    if(mod_main && PyCallable_Check(mod_main)){
        //maybe unpack args directly? probably easier to use a dict
        PyObject *py_args = PyTuple_New(3);
        PyObject *pystr_mod_name = py_unicode_or_string(module_name);
        PyTuple_SetItem(py_args, 0, pystr_mod_name);
        PyObject *py_flux_handle = PyLong_FromUintptr_t((uintptr_t)h);
        if (py_flux_handle == NULL)
          return -1;
        PyTuple_SetItem(py_args, 1, py_flux_handle);

        //Convert zhash to native python dict, should preserve mods
        //through switch to argc-style arguments
        PyObject *arg_list = PyList_New(0);
        char ** it = argv + option_index;
        int i;
        for (i=0; *it; i++, it++){
            PyList_Append(arg_list, py_unicode_or_string(*it));
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
