/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* libsdprocess
 *
 * The libsdprocess library is a library to launch and monitor
 * processes under systemd.  It abstracts away the underlying sd-bus
 * API calls into a higher level API.  Monitoring of a process is
 * integrated into the flux reactor by using a fd watcher and the file
 * descriptor provided by sd_bus_get_fd().
 *
 * A good portion of the code here is modeled after the code found in
 * the tools `systemd-run` and `systemctl`.
 *
 * Additional information can be found in the following documentation:
 *
 * DBUS spec: https://dbus.freedesktop.org/doc/dbus-specification.html
 *
 * org.freedesktop.systemd1:
 * https://www.freedesktop.org/software/systemd/man/org.freedesktop.systemd1.html
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <flux/core.h>
#include <assert.h>

#include <systemd/sd-bus.h>

#include "sdprocess.h"
#include "strv.h"

struct sdprocess {
    flux_t *h;
    flux_reactor_t *reactor;
    char *unitname;
    char **argv;
    char **envv;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;

    sd_bus *bus;
    /* save previous bus in reconnect case */
    sd_bus *bus_prev;
    /* <unitname>.service */
    char *service_name;
    /* /org/freedesktop/systemd1/unit/<unitname>_2eservice */
    char *service_path;

    /* state watcher */
    flux_watcher_t *w_state;
    flux_watcher_t *w_state_prep;
    flux_watcher_t *w_state_idle;
    flux_watcher_t *w_state_check;
    bool active;
    bool active_sent;
    bool exited;
    bool exited_sent;
    sdprocess_state_f state_cb;
    void *state_cb_arg;
    uint32_t exec_main_status;
    uint32_t exec_main_code;
    char *active_state;
    char *result;
    int wait_status;
};

static bool sdprocess_log_enable = true;

static int sdprocess_state_setup (sdprocess_t *sdp,
                                  sdprocess_state_f state_cb,
                                  void *arg,
                                  flux_reactor_t *reactor);

void sdprocess_logging (bool enable)
{
    sdprocess_log_enable = enable;
}

static void sdp_log (flux_t *h, int level, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    if (sdprocess_log_enable)
        flux_vlog (h, level, fmt, ap);
    va_end (ap);
}

static void sdp_log_error (flux_t *h, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    if (sdprocess_log_enable)
        flux_log_verror (h, fmt, ap);
    va_end (ap);
}

static void set_errno_log (flux_t *h,
                           int ret,
                           const char *prefix)
{
    errno = -ret;
    sdp_log_error (h, "%s", prefix);
}

static void set_errno_log_errmsg (flux_t *h,
                                  int ret,
                                  sd_bus_error *error,
                                  const char *prefix)
{
    assert (error);
    errno = -ret;
    if (error->message)
        sdp_log (h, LOG_ERR, "%s: %s", prefix, error->message);
    else
        sdp_log_error (h, "%s", prefix);
}

void sdprocess_destroy (sdprocess_t *sdp)
{
    if (sdp) {
        int save_errno = errno;
        free (sdp->unitname);
        strv_destroy (sdp->argv);
        strv_destroy (sdp->envv);

        sd_bus_close (sdp->bus);
        sd_bus_unref (sdp->bus);
        sd_bus_close (sdp->bus_prev);
        sd_bus_unref (sdp->bus_prev);
        free (sdp->service_name);
        free (sdp->service_path);

        flux_watcher_destroy (sdp->w_state);
        flux_watcher_destroy (sdp->w_state_prep);
        flux_watcher_destroy (sdp->w_state_idle);
        flux_watcher_destroy (sdp->w_state_check);

        free (sdp->active_state);
        free (sdp->result);
        free (sdp);
        errno = save_errno;
    }
}

static char *calc_service_path (const char *unitname)
{
    /* note that lower case should be used, not upper case hex.  Apparently
     * a part of dbus standard.
     */
    char hex2char[16] = "0123456789abcdef";
    const char *service_path_prefix = "/org/freedesktop/systemd1/unit/";
    int service_path_prefix_len = strlen (service_path_prefix);
    /* _2e is escape of . (period) */
    const char *service_path_suffix = "_2eservice";
    int service_path_suffix_len = strlen (service_path_suffix);
    const char *ptr;
    char *service_path;
    int index = 0;
    int len = 0;

    /* we must escape all special chars in the service path b/c dbus
     * cannot handle them.
     *
     * escape rule is <special char> -> _<hex bytes of char>.
     * e.g. . -> _2e
     *
     * note that a-f should be lower case
     *
     * also escape 0-9 if numeral is first character in unitname.
     */

    /* buffer length for path is:
     *
     * path prefix length
     * + length unitname * 3 (*3 if all chars need to be escaped)
     * + path suffix length
     * + 1 (NUL byte)
     */
    len = service_path_prefix_len;
    len += strlen (unitname) * 3;
    len += service_path_suffix_len;
    len += 1;

    if (!(service_path = calloc (1, len)))
        return NULL;

    strcpy (service_path, service_path_prefix);
    index += service_path_prefix_len;

    ptr = unitname;
    while (*ptr) {
        if (!((*ptr) >= 'A' && (*ptr) <= 'Z')
            && !((*ptr) >= 'a' && (*ptr) <= 'z')
            && !(ptr > unitname && (*ptr) >= '0' && (*ptr) <= '9')) {
            service_path[index++] = '_';
            service_path[index++] = hex2char[*ptr >> 4];
            service_path[index++] = hex2char[*ptr & 0xF];
        }
        else
            service_path[index++] = *ptr;
        ptr++;
    }

    strcpy (service_path + index, service_path_suffix);

    return service_path;
}

static sdprocess_t *sdprocess_create (flux_t *h,
                                      const char *unitname,
                                      char **argv,
                                      char **envv,
                                      int stdin_fd,
                                      int stdout_fd,
                                      int stderr_fd)
{
    struct sdprocess *sdp = NULL;
    int ret;

    if (!(sdp = calloc (1, sizeof (*sdp))))
        return NULL;
    if (!(sdp->unitname = strdup (unitname)))
        goto cleanup;
    if (argv) {
        if (strv_copy (argv, &sdp->argv) < 0)
            goto cleanup;
    }
    if (envv) {
        if (strv_copy (envv, &sdp->envv) < 0)
            goto cleanup;
    }
    sdp->h = h;
    if (!(sdp->reactor = flux_get_reactor (sdp->h)))
        goto cleanup;
    sdp->stdin_fd = stdin_fd;
    sdp->stdout_fd = stdout_fd;
    sdp->stderr_fd = stderr_fd;

    if ((ret = sd_bus_open_user (&sdp->bus)) < 0) {
        errno = -ret;
        goto cleanup;
    }
    if (asprintf (&sdp->service_name, "%s.service", sdp->unitname) < 0)
        goto cleanup;
    if (!(sdp->service_path = calc_service_path (sdp->unitname)))
        goto cleanup;

    return sdp;

cleanup:
    sdprocess_destroy (sdp);
    return NULL;
}

static int transient_service_set_stdio_properties (sdprocess_t *sdp,
                                                   sd_bus_message *m)
{
    int ret;

    if (sdp->stdin_fd >= 0) {
        if ((ret = sd_bus_message_append (m,
                                          "(sv)",
                                          "StandardInputFileDescriptor",
                                          "h",
                                          sdp->stdin_fd)) < 0)
            goto error;
    }

    if (sdp->stdout_fd >= 0) {
        if ((ret = sd_bus_message_append (m,
                                          "(sv)",
                                          "StandardOutputFileDescriptor",
                                          "h",
                                          sdp->stdout_fd)) < 0)
            goto error;
    }

    if (sdp->stderr_fd >= 0) {
        if ((ret = sd_bus_message_append (m,
                                          "(sv)",
                                          "StandardErrorFileDescriptor",
                                          "h",
                                          sdp->stderr_fd)) < 0)
            goto error;
    }

    return 0;

error:
    set_errno_log (sdp->h, ret, "error setup stdio properties");
    return -1;
}

static int transient_service_set_environment_properties (sdprocess_t *sdp,
                                                         sd_bus_message *m)
{
    int ret;

    if (sdp->envv) {

        if ((ret = sd_bus_message_open_container (m, 'r', "sv")) < 0)
            goto error;

        if ((ret = sd_bus_message_append (m, "s", "Environment")) < 0)
            goto error;

        if ((ret = sd_bus_message_open_container (m, 'v', "as")) < 0)
            goto error;

        if ((ret = sd_bus_message_append_strv (m, sdp->envv)) < 0)
            goto error;

        if ((ret = sd_bus_message_close_container (m)) < 0)
            goto error;

        if ((ret = sd_bus_message_close_container (m)) < 0)
            goto error;
    }

    return 0;

error:
    set_errno_log (sdp->h, ret, "error setup environment properties");
    return -1;
}

static int transient_service_set_cmdline_properties (sdprocess_t *sdp,
                                                     sd_bus_message *m)
{
    int ret;

    if ((ret = sd_bus_message_open_container (m, 'r', "sv")) < 0)
        goto error;

    if ((ret = sd_bus_message_append (m, "s", "ExecStart")) < 0)
        goto error;

    if ((ret = sd_bus_message_open_container (m, 'v', "a(sasb)")) < 0)
        goto error;

    if ((ret = sd_bus_message_open_container (m, 'a', "(sasb)")) < 0)
        goto error;

    if ((ret = sd_bus_message_open_container (m, 'r', "sasb")) < 0)
        goto error;

    if ((ret = sd_bus_message_append (m, "s", sdp->argv[0])) < 0)
        goto error;

    if ((ret = sd_bus_message_append_strv (m, sdp->argv)) < 0)
        goto error;

    if ((ret = sd_bus_message_append (m, "b", false)) < 0)
        goto error;

    if ((ret = sd_bus_message_close_container (m)) < 0)
        goto error;

    if ((ret = sd_bus_message_close_container (m)) < 0)
        goto error;

    if ((ret = sd_bus_message_close_container (m)) < 0)
        goto error;

    if ((ret = sd_bus_message_close_container (m)) < 0)
        goto error;

    return 0;

error:
    set_errno_log (sdp->h, ret, "error setup cmdline properties");
    return -1;
}

static int transient_service_set_properties (sdprocess_t *sdp,
                                             sd_bus_message *m)
{
    int ret;

    if ((ret = sd_bus_message_append (m,
                                      "(sv)",
                                      "Description",
                                      "s",
                                      "libsdprocess")) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_append");
        return -1;
    }

    /* achu: no property assignments for the time being */

    if ((ret = sd_bus_message_append (m, "(sv)", "AddRef", "b", 1)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_append");
        return -1;
    }

    /* In sdprocess, we require the systemd unit to exist until the
     * user cleans it up with sdprocess_systemd_cleanup().  This
     * ensures consistent behavior in a number of functions.  For
     * example, a function like sdprocess_wait() can be called
     * multiple times.  Therefore, we set RemainAfterExit to true
     * for every process we start.
     */
    if ((ret = sd_bus_message_append (m,
                                      "(sv)",
                                      "RemainAfterExit",
                                      "b",
                                      true)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_append");
        return -1;
    }

    /* Stdio */

    if (transient_service_set_stdio_properties (sdp, m) < 0)
        return -1;

    /* Environment */

    if (transient_service_set_environment_properties (sdp, m) < 0)
        return -1;

    /* Cmdline */

    if (transient_service_set_cmdline_properties (sdp, m) < 0)
        return -1;

    return 0;
}

static int start_transient_service (sdprocess_t *sdp)
{

    sd_bus_message *m = NULL, *reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int save_errno, ret, rv = -1;

    if ((ret = sd_bus_message_new_method_call (
                                        sdp->bus,
                                        &m,
                                        "org.freedesktop.systemd1",
                                        "/org/freedesktop/systemd1",
                                        "org.freedesktop.systemd1.Manager",
                                        "StartTransientUnit")) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_new_method_call");
        goto cleanup;
    }

    /* Name and mode */
    if ((ret = sd_bus_message_append (m,
                                      "ss",
                                      sdp->service_name,
                                      "fail")) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_append");
        goto cleanup;
    }

    /* Properties */
    if ((ret = sd_bus_message_open_container (m, 'a', "(sv)")) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_open_container");
        goto cleanup;
    }

    if ((ret = transient_service_set_properties (sdp, m)) < 0)
        goto cleanup;

    if ((ret = sd_bus_message_close_container (m)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_close_container");
        goto cleanup;
    }

    /* Auxiliary units */
    if ((ret = sd_bus_message_append (m, "a(sa(sv))", 0)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_append");
        goto cleanup;
    }

    if ((ret = sd_bus_call (sdp->bus, m, 0, &error, &reply)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_call");
        goto cleanup;
    }

    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_message_unref (m);
    sd_bus_message_unref (reply);
    sd_bus_error_free (&error);
    errno = save_errno;
    return rv;
}

sdprocess_t *sdprocess_exec (flux_t *h,
                             const char *unitname,
                             char **argv,
                             char **envv,
                             int stdin_fd,
                             int stdout_fd,
                             int stderr_fd)
{
    struct sdprocess *sdp = NULL;

    if (!h
        || !unitname
        || strlen (unitname) == 0
        || !argv) {
        errno = EINVAL;
        return NULL;
    }

    if (!(sdp = sdprocess_create (h,
                                  unitname,
                                  argv,
                                  envv,
                                  stdin_fd,
                                  stdout_fd,
                                  stderr_fd)))
        return NULL;

    if (start_transient_service (sdp) < 0)
        goto cleanup;

    return sdp;

cleanup:
    sdprocess_destroy (sdp);
    return NULL;
}

static int check_exist (sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *load_state = NULL;
    int save_errno, ret, rv = -1;

    if ((ret = sd_bus_get_property_string (sdp->bus,
                                           "org.freedesktop.systemd1",
                                           sdp->service_path,
                                           "org.freedesktop.systemd1.Unit",
                                           "LoadState",
                                           &error,
                                           &load_state)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_get_property_string");
        goto cleanup;
    }

    if (!strcmp (load_state, "not-found")) {
        errno = ENOENT;
        goto cleanup;
    }

    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    free (load_state);
    errno = save_errno;
    return rv;
}

sdprocess_t *sdprocess_find_unit (flux_t *h, const char *unitname)
{
    struct sdprocess *sdp = NULL;

    if (!h || !unitname) {
        errno = EINVAL;
        return NULL;
    }

    if (!(sdp = sdprocess_create (h,
                                  unitname,
                                  NULL,
                                  NULL,
                                  -1,
                                  -1,
                                  -1)))
        return NULL;

    if (check_exist (sdp) < 0)
        goto cleanup;

    return sdp;

cleanup:
    sdprocess_destroy (sdp);
    return NULL;
}

static int get_final_properties (sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    uint32_t exec_main_status;
    char *result = NULL;
    int save_errno, ret, rv = -1;

    if ((ret = sd_bus_get_property_trivial (sdp->bus,
                                            "org.freedesktop.systemd1",
                                            sdp->service_path,
                                            "org.freedesktop.systemd1.Service",
                                            "ExecMainStatus",
                                            &error,
                                            'i',
                                            &exec_main_status)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_get_property_trivial");
        goto cleanup;
    }

    if ((ret = sd_bus_get_property_string (sdp->bus,
                                           "org.freedesktop.systemd1",
                                           sdp->service_path,
                                           "org.freedesktop.systemd1.Service",
                                           "Result",
                                           &error,
                                           &result)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_get_property_string");
        goto cleanup;
    }

    sdp->exec_main_status = exec_main_status;
    if (sdp->result)
        free (sdp->result);
    sdp->result = result;
    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    if (rv < 0)
        free (result);
    errno = save_errno;
    return rv;
}

static void calc_wait_status (sdprocess_t *sdp)
{
    assert (sdp->result);
    if (!strcmp (sdp->result, "signal"))
        sdp->wait_status = __W_EXITCODE (0, sdp->exec_main_status);
    else
        sdp->wait_status = __W_EXITCODE (sdp->exec_main_status, 0);
}

static bool check_exec_main_code_unit_done (int exec_main_code)
{
    if (exec_main_code == CLD_EXITED
        || exec_main_code == CLD_KILLED
        || exec_main_code == CLD_DUMPED)
        return true;
    return false;
}

static int get_properties_string (sdprocess_t *sdp,
                                  sd_bus_message *m,
                                  char **strp)
{
    const char *contents;
    const char *str;
    char type;
    int ret;

    if ((ret = sd_bus_message_peek_type (m, NULL, &contents)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_peek_type");
        return -1;
    }

    if ((ret = sd_bus_message_enter_container (m,
                                               SD_BUS_TYPE_VARIANT,
                                               contents)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_enter_container");
        return -1;
    }

    if ((ret = sd_bus_message_peek_type (m, &type, NULL)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_peek_type");
        return -1;
    }

    if (type != SD_BUS_TYPE_STRING) {
        /* XXX right errno?*/
        errno = EFAULT;
        sdp_log (sdp->h, LOG_DEBUG, "Invalid type %d, expected %d",
                 type, SD_BUS_TYPE_STRING);
        return -1;
    }

    if ((ret = sd_bus_message_read_basic (m, type, &str)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_read_basic");
        return -1;
    }

    if (!(*strp)
        || strcmp ((*strp), str) != 0) {
        char *tmp;
        if (!(tmp = strdup (str)))
            return -1;
        free (*strp);
        (*strp) = tmp;
    }

    if ((ret = sd_bus_message_exit_container (m)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_exit_container");
        return -1;
    }

    return 0;
}

static int get_properties_int (sdprocess_t *sdp,
                               sd_bus_message *m,
                               uint32_t *valp)
{
    const char *contents;
    uint32_t val;
    char type;
    int ret;

    if ((ret = sd_bus_message_peek_type (m, NULL, &contents)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_peek_type");
        return -1;
    }

    if ((ret = sd_bus_message_enter_container (m,
                                               SD_BUS_TYPE_VARIANT,
                                               contents)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_enter_container");
        return -1;
    }

    if ((ret = sd_bus_message_peek_type (m, &type, NULL)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_peek_type");
        return -1;
    }

    if (type != SD_BUS_TYPE_INT32) {
        /* XXX right errno?*/
        errno = EFAULT;
        sdp_log (sdp->h, LOG_DEBUG, "Invalid type %d, expected %d",
                 type, SD_BUS_TYPE_INT32);
        return -1;
    }

    if ((ret = sd_bus_message_read_basic (m,
                                          type,
                                          &val)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_read_basic");
        return -1;
    }

    if ((*valp) != val)
        (*valp) = val;

    if ((ret = sd_bus_message_exit_container (m)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_exit_container");
        return -1;
    }

    return 0;
}

static int get_properties_changed (sdprocess_t *sdp)
{
    sd_bus_message *m = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int save_errno, ret, rv = -1;

    if ((ret = sd_bus_call_method (sdp->bus,
                                   "org.freedesktop.systemd1",
                                   sdp->service_path,
                                   "org.freedesktop.DBus.Properties",
                                   "GetAll",
                                   &error,
                                   &m,
                                   "s",
                                   "")) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_call_method");
        goto cleanup;
    }

    if ((ret = sd_bus_message_enter_container (m,
                                               SD_BUS_TYPE_ARRAY,
                                               "{sv}")) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_enter_container");
        goto cleanup;
    }

    while ((ret = sd_bus_message_enter_container (m,
                                                  SD_BUS_TYPE_DICT_ENTRY,
                                                  "sv")) > 0) {
        const char *member;

        if ((ret = sd_bus_message_read_basic (m,
                                              SD_BUS_TYPE_STRING,
                                              &member)) < 0) {
            set_errno_log (sdp->h, ret, "sd_bus_message_read_basic");
            goto cleanup;
        }

        if (!strcmp (member, "ActiveState")) {
            if (get_properties_string (sdp, m, &(sdp->active_state)) < 0)
                goto cleanup;
        }
        else if (!strcmp (member, "Result")) {
            if (get_properties_string (sdp, m, &(sdp->result)) < 0)
                goto cleanup;
        }
        else if (!strcmp (member, "ExecMainStatus")) {
            if (get_properties_int (sdp, m, &(sdp->exec_main_status)) < 0)
                goto cleanup;
        }
        else if (!strcmp (member, "ExecMainCode")) {
            if (get_properties_int (sdp, m, &(sdp->exec_main_code)) < 0)
                goto cleanup;
        }
        else {
            if ((ret = sd_bus_message_skip (m, "v")) < 0) {
                set_errno_log (sdp->h, ret, "sd_bus_message_skip");
                goto cleanup;
            }
        }

        if ((ret = sd_bus_message_exit_container (m)) < 0) {
            set_errno_log (sdp->h, ret, "sd_bus_message_exit_container");
            goto cleanup;
        }
    }
    if (ret < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_enter_container");
        goto cleanup;
    }

    if ((ret = sd_bus_message_exit_container (m)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_message_exit_container");
        goto cleanup;
    }

    if ((sdp->active_state &&
         !strcmp (sdp->active_state, "failed"))
        || check_exec_main_code_unit_done (sdp->exec_main_code)) {
        calc_wait_status (sdp);
        sdp->exited = true;
    }
    else if ((sdp->active_state &&
              !strcmp (sdp->active_state, "active")))
        sdp->active = true;

    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_message_unref (m);
    sd_bus_error_free (&error);
    errno = save_errno;
    return rv;
}

static int sdbus_properties_changed_cb (sd_bus_message *m,
                                        void *userdata,
                                        sd_bus_error *error)
{
    sdprocess_t *sdp = userdata;
    return get_properties_changed (sdp);
}

static void watcher_properties_changed_cb (flux_reactor_t *r,
                                           flux_watcher_t *w,
                                           int revents,
                                           void *arg)
{
    sdprocess_t *sdp = arg;

    if (revents & FLUX_POLLIN) {
        while (1) {
            int ret = sd_bus_process (sdp->bus, NULL);
            if (ret <= 0) {
                if (ret < 0) {
                    set_errno_log (sdp->h, ret, "sd_bus_process");

                    /* On systemd < v240, the default
                     * BUS_DEFAULT_TIMEOUT is 25 seconds.  If the
                     * process that is executed runs longer than 25
                     * seconds, the bus could become disconnected and
                     * we get a ECONNRESET.  To workaround this issue,
                     * we re-establish the bus connection and re-setup
                     * watchers.
                     *
                     * In newer versions of systemd the environment
                     * variable SYSTEMD_BUS_TIMEOUT or a call to
                     * sd_bus_method_call_timeout() can resolve this
                     * issue.
                     */

                    if (ret == -ECONNRESET) {
                        /* unsure if saving current bus to bus_prev is
                         * necessary, but since we're still in a
                         * callback from fd of the prior bus, we
                         * probably should not close/unref it until
                         * later.
                         *
                         * We only close/unref if there was a prior
                         * ECONNRESET before this one.
                         */
                        if (sdp->bus_prev) {
                            sd_bus_close (sdp->bus_prev);
                            sd_bus_unref (sdp->bus_prev);
                        }
                        sdp->bus_prev = sdp->bus;
                        sdp->bus = NULL;

                        /* XXX in event of error, how alert caller? */
                        if ((ret = sd_bus_open_user (&sdp->bus)) < 0) {
                            set_errno_log (sdp->h, ret, "sd_bus_open_user");
                            break;
                        }

                        if (sdprocess_state_setup (sdp,
                                                   sdp->state_cb,
                                                   sdp->state_cb_arg,
                                                   sdp->reactor) < 0)
                            break;

                        if (sdp->exited) {
                            /* achu: stopping these watchers is
                             * precautionary, I can't imagine a
                             * situation in which the exited_sent
                             * would be true at this point.
                             */
                            if (sdp->exited_sent) {
                                flux_watcher_stop (sdp->w_state_prep);
                                flux_watcher_stop (sdp->w_state_check);
                            }
                            break;
                        }
                    }
                }
                break;
            }
        }
    }
    else {
        sdp_log (sdp->h, LOG_DEBUG, "Unexpected revents: %X", revents);
        return;
    }

    if (sdp->exited)
        flux_watcher_stop (w);
}

/* convert usec to millisecond, but with hacks to ensure we round up
 * and handle a few corner cases */
static int usec_to_ms (uint64_t usec)
{
    int timeout = 0;
    if (usec) {
        if (usec >= (UINT64_MAX - 1000))
            timeout = INT_MAX;
        else {
            uint64_t tmp;
            if ((usec % 1000) != 0)
                tmp = (usec + 1000) / 1000;
            else
                tmp = usec / 1000;

            if (tmp > INT_MAX)
                timeout = INT_MAX;
            else
                timeout = tmp;
        }
    }
    return timeout;
}

static int setup_state_watcher (sdprocess_t *sdp, flux_reactor_t *reactor)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int fd;
    short events;
    int timeout = -1;
    int save_errno, ret, rv = -1;

    /* Subscribe to events on this systemd1 manager */
    if ((ret = sd_bus_call_method (sdp->bus,
                                   "org.freedesktop.systemd1",
                                   "/org/freedesktop/systemd1",
                                   "org.freedesktop.systemd1.Manager",
                                   "Subscribe",
                                   &error,
                                   NULL,
                                   NULL)) < 0) {
        if (!error.name
            || strcmp (error.name,
                       "org.freedesktop.systemd1.AlreadySubscribed") != 0) {
            set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_call_method");
            goto cleanup;
        }
    }

    /* Available in systemd v240 and newer */
#if HAVE_SD_BUS_SET_METHOD_CALL_TIMEOUT
    if ((ret = sd_bus_set_method_call_timeout (sdp->bus, ULONG_MAX)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_set_method_call_timeout");
        goto cleanup;
    }
#endif

    /* Setup callback when `sd_bus_process ()` is called on properties
     * changed */
    if ((ret = sd_bus_match_signal (sdp->bus,
                                    NULL,
                                    "org.freedesktop.systemd1",
                                    sdp->service_path,
                                    "org.freedesktop.DBus.Properties",
                                    "PropertiesChanged",
                                    sdbus_properties_changed_cb,
                                    sdp)) < 0) {
        set_errno_log (sdp->h, ret, "sd_bus_match_signal");
        goto cleanup;
    }

    while (1) {
        uint64_t usec;
        fd = sd_bus_get_fd (sdp->bus);
        events = sd_bus_get_events (sdp->bus);
        if (sd_bus_get_timeout (sdp->bus, &usec) >= 0)
            timeout = usec_to_ms (usec);

        /* if no events or no timeout, assume event ready to go right
         * now
         *
         * we don't handle ECONNRESET here, assuming we won't timeout
         * just after setting up callback via
         * sd_bus_match_signal() above.
         */
        if (!events || !timeout) {
            while (1) {
                ret = sd_bus_process (sdp->bus, NULL);
                if (ret < 0) {
                    set_errno_log (sdp->h, ret, "sd_bus_process");
                    goto cleanup;
                }
                if (!ret)
                    break;
            }
            continue;
        }
        break;
    }

    if (sdp->exited)
        goto done;

    if (sdp->w_state)
        flux_watcher_destroy (sdp->w_state);

    /* Assumption: bus will never change fd */
    if (!(sdp->w_state = flux_fd_watcher_create (reactor,
                                                 fd,
                                                 events,
                                                 watcher_properties_changed_cb,
                                                 sdp))) {
        sdp_log_error (sdp->h, "flux_fd_watcher_create");
        goto cleanup;
    }
    flux_watcher_start (sdp->w_state);
done:
    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    errno = save_errno;
    return rv;
}

static char *get_active_state (sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    char *rv = NULL;
    int save_errno, ret;

    if ((ret = sd_bus_get_property_string (sdp->bus,
                                           "org.freedesktop.systemd1",
                                           sdp->service_path,
                                           "org.freedesktop.systemd1.Unit",
                                           "ActiveState",
                                           &error,
                                           &active_state)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_get_property_string");
        goto cleanup;
    }

    rv = active_state;
    active_state = NULL;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    free (active_state);
    errno = save_errno;
    return rv;
}

static int get_exec_main_code (sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int exec_main_code;
    int save_errno, ret, rv = -1;

    if ((ret = sd_bus_get_property_trivial (sdp->bus,
                                            "org.freedesktop.systemd1",
                                            sdp->service_path,
                                            "org.freedesktop.systemd1.Service",
                                            "ExecMainCode",
                                            &error,
                                            'i',
                                            &exec_main_code)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_get_property_trivial");
        goto cleanup;
    }

    rv = exec_main_code;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    errno = save_errno;
    return rv;
}

static int check_state (sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    int save_errno, rv = -1;

    if (!(active_state = get_active_state (sdp)))
        goto cleanup;

    /* Assumption: All other states "inactive", "activating",
     * "deactivating", "reloaded", we are transitioning to a final
     * state of "active" (success w/ RemainAfterExit) or
     * "failed" */
    if (!strcmp (active_state, "failed")) {
        if (get_final_properties (sdp) < 0)
            goto cleanup;
        calc_wait_status (sdp);
        sdp->exited = true;
        goto done;
    }
    else if (strcmp (active_state, "active")) {
        errno = EAGAIN;
        goto cleanup;
    }

    if (!strcmp (active_state, "active")) {
        /* If unit is still active, possible it is done b/c of
         * RemainAfterExit, so check if unit exited.
         */
        int exec_main_code;

        if ((exec_main_code = get_exec_main_code (sdp)) < 0)
            goto cleanup;

        if (check_exec_main_code_unit_done (exec_main_code)) {
            if (get_final_properties (sdp) < 0)
                goto cleanup;
            calc_wait_status (sdp);
            sdp->exited = true;
        }
        else
            sdp->active = true;
    }

done:
    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    free (active_state);
    errno = save_errno;
    return rv;
}

static void state_change_prep_cb (flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg)
{
    sdprocess_t *sdp = arg;
    if ((sdp->active && !sdp->active_sent)
        || (sdp->exited && !sdp->exited_sent))
        flux_watcher_start (sdp->w_state_idle);
}

static void state_change_check_cb (flux_reactor_t *r,
                                   flux_watcher_t *w,
                                   int revents,
                                   void *arg)
{
    sdprocess_t *sdp = arg;
    flux_watcher_stop (sdp->w_state_idle);
    if (sdp->active
        && !sdp->active_sent
        && !(sdp->exited && !sdp->exited_sent)) {
        if (sdp->state_cb)
            sdp->state_cb (sdp, SDPROCESS_ACTIVE, sdp->state_cb_arg);
        sdp->active_sent = true;
    }
    if (sdp->exited && !sdp->exited_sent) {
        if (sdp->state_cb)
            sdp->state_cb (sdp, SDPROCESS_EXITED, sdp->state_cb_arg);
        sdp->exited_sent = true;
        flux_watcher_stop (sdp->w_state_prep);
        flux_watcher_stop (sdp->w_state_check);
    }
}

static int setup_state_change_callbacks (sdprocess_t *sdp,
                                         sdprocess_state_f state_cb,
                                         void *arg,
                                         flux_reactor_t *reactor)
{
    sdp->state_cb = state_cb;
    sdp->state_cb_arg = arg;

    if (sdp->w_state_prep)
        flux_watcher_destroy (sdp->w_state_prep);
    sdp->w_state_prep = flux_prepare_watcher_create (reactor,
                                                     state_change_prep_cb,
                                                     sdp);
    if (!sdp->w_state_prep) {
        sdp_log_error (sdp->h, "flux_prepare_watcher_create");
        return -1;
    }

    if (sdp->w_state_idle)
        flux_watcher_destroy (sdp->w_state_idle);
    sdp->w_state_idle = flux_idle_watcher_create (reactor,
                                                  NULL,
                                                  sdp);
    if (!sdp->w_state_idle) {
        sdp_log_error (sdp->h, "flux_idle_watcher_create");
        return -1;
    }

    if (sdp->w_state_check)
        flux_watcher_destroy (sdp->w_state_check);
    sdp->w_state_check = flux_check_watcher_create (reactor,
                                                    state_change_check_cb,
                                                    sdp);
    if (!sdp->w_state_check) {
        sdp_log_error (sdp->h, "flux_check_watcher_create");
        return -1;
    }

    flux_watcher_start (sdp->w_state_prep);
    flux_watcher_start (sdp->w_state_check);
    return 0;
}

static int sdprocess_state_setup (sdprocess_t *sdp,
                                  sdprocess_state_f state_cb,
                                  void *arg,
                                  flux_reactor_t *reactor)
{
    assert (sdp);
    assert (reactor);

    /* if called earlier */
    sdp->active = false;
    sdp->active_sent = false;
    sdp->exited = false;
    sdp->exited_sent = false;

    if (check_exist (sdp) < 0)
        return -1;

    if (check_state (sdp) < 0) {
        /* if we're transitioning between states, fallthrough and let
         * code logic handle active state transitions
         */
        if (errno != EAGAIN)
            return -1;
    }

    if (setup_state_change_callbacks (sdp, state_cb, arg, reactor) < 0)
        return -1;

    if (sdp->exited)
        return 0;
    if (setup_state_watcher (sdp, reactor) < 0) {
        flux_watcher_stop (sdp->w_state_prep);
        flux_watcher_stop (sdp->w_state_check);
        return -1;
    }

    if (sdp->exited)
        return 0;

    /* Small racy window in which job exited all state changes
     * after we called check_state() above, but before we finished
     * setting up in setup_state_watcher().  So no state changes
     * will ever occur going forward.  Call check_state() again
     * just in case.
     */

    if (!sdp->active_state) {
        if (check_state (sdp) < 0) {
            /* if we're transitioning between states, fallthrough and
             * let code logic handle active state transitions
             */
            if (errno != EAGAIN) {
                flux_watcher_stop (sdp->w_state);
                flux_watcher_stop (sdp->w_state_prep);
                flux_watcher_stop (sdp->w_state_check);
                return -1;
            }
        }
        if (sdp->exited) {
            flux_watcher_stop (sdp->w_state);
            return 0;
        }
    }

    return 0;
}

int sdprocess_state (sdprocess_t *sdp,
                     sdprocess_state_f state_cb,
                     void *arg)
{
    if (!sdp
        || !state_cb) {
        errno = EINVAL;
        return -1;
    }

    return sdprocess_state_setup (sdp, state_cb, arg, sdp->reactor);
}

int sdprocess_wait (sdprocess_t *sdp)
{
    flux_reactor_t *tmp_reactor;
    int rc;

    /* XXX - potential corner case, what if user calls
     * sdprocess_state() and then sdprocess_wait()?
     */

    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (!(tmp_reactor = flux_reactor_create (0))) {
        sdp_log_error (sdp->h, "flux_reactor_create");
        return -1;
    }
    if (sdprocess_state_setup (sdp, NULL, sdp, tmp_reactor) < 0) {
        flux_reactor_destroy (tmp_reactor);
        return -1;
    }
    rc = flux_reactor_run (tmp_reactor, 0);
    flux_reactor_destroy (tmp_reactor);
    return rc;
}

const char *sdprocess_unitname (sdprocess_t *sdp)
{
    if (!sdp) {
        errno = EINVAL;
        return NULL;
    }

    return sdp->unitname;
}

int sdprocess_pid (sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    unsigned int pid;
    int save_errno, ret, rv = -1;

    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (!(active_state = get_active_state (sdp)))
        goto cleanup;

    if (strcmp (active_state, "active")) {
        /* XXX right errno? */
        errno = EPERM;
        goto cleanup;
    }

    if ((ret = sd_bus_get_property_trivial (sdp->bus,
                                            "org.freedesktop.systemd1",
                                            sdp->service_path,
                                            "org.freedesktop.systemd1.Service",
                                            "MainPID",
                                            &error,
                                            'u',
                                            &pid)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_get_property_trivial");
        goto cleanup;
    }

    rv = pid;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    free (active_state);
    errno = save_errno;
    return rv;
}

static int check_active (sdprocess_t *sdp, bool *is_active)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    int save_errno, rv = -1;

    if (!(active_state = get_active_state (sdp)))
        goto cleanup;

    (*is_active) = strcmp (active_state, "active") == 0 ? true : false;
    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    free (active_state);
    errno = save_errno;
    return rv;
}

bool sdprocess_active (sdprocess_t *sdp)
{
    bool is_active = false;

    if (!sdp)
        return false;

    if (check_active (sdp, &is_active) < 0)
        return false;
    return is_active;
}

bool sdprocess_exited (sdprocess_t *sdp)
{
    if (!sdp)
        return false;

    if (!sdp->exited) {
        if (check_state (sdp) < 0)
            return false;
    }

    return sdp->exited;
}

int sdprocess_exit_status (sdprocess_t *sdp)
{
    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (!sdp->exited) {
        /* see if it has exited */
        if (check_state (sdp) < 0) {
            if (errno != EAGAIN)
                return -1;
        }
        if (!sdp->exited) {
            /* XXX right errno? */
            errno = EBUSY;
            return -1;
        }
    }

    return sdp->exec_main_status;
}

int sdprocess_wait_status (sdprocess_t *sdp)
{
    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (!sdp->exited) {
        /* see if it has exited */
        if (check_state (sdp) < 0) {
            if (errno != EAGAIN)
                return -1;
        }
        if (!sdp->exited) {
            /* XXX right errno? */
            errno = EBUSY;
            return -1;
        }
    }

    return sdp->wait_status;
}

int sdprocess_kill (sdprocess_t *sdp, int signo)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    int save_errno, ret, rv = -1;

    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    /* Loosely the equivalent of systemctl kill ... */

    if (!(active_state = get_active_state (sdp)))
        goto cleanup;

    if (strcmp (active_state, "active")) {
        /* XXX right errno? */
        errno = EPERM;
        goto cleanup;
    }

    if ((ret = sd_bus_call_method (sdp->bus,
                                   "org.freedesktop.systemd1",
                                   "/org/freedesktop/systemd1",
                                   "org.freedesktop.systemd1.Manager",
                                   "KillUnit",
                                   &error,
                                   NULL,
                                   "ssi",
                                   sdp->service_name,
                                   "all",
                                   signo)) < 0) {
        set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_call_method");
        goto cleanup;
    }

    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    free (active_state);
    errno = save_errno;
    return rv;
}

int sdprocess_systemd_cleanup (sdprocess_t *sdp)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    char *active_state = NULL;
    int save_errno, ret, rv = -1;

    if (!sdp) {
        errno = EINVAL;
        return -1;
    }

    if (check_exist (sdp) < 0)
        return -1;

    if (!(active_state = get_active_state (sdp)))
        goto cleanup;

    if (!strcmp (active_state, "active")) {
        int exec_main_code;

        /* Due to "RemainAfterExit", an exited successful process will
         * stay "active".  So we gotta make sure its actually exited.
         * Observation is that a state can go to "inactive", then back
         * to "active" b/c of "RemainAfterExit".
         */

        if ((exec_main_code = get_exec_main_code (sdp)) < 0)
            goto cleanup;

        if (!exec_main_code) {
            /* XXX right errno? */
            errno = EBUSY;
            goto cleanup;
        }

        /* This is loosely the equivalent of:
         *
         * systemctl stop --user <unitname>.service
         */
        if ((ret = sd_bus_call_method (sdp->bus,
                                       "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "StopUnit",
                                       &error,
                                       NULL,
                                       "ss",
                                       sdp->service_name,
                                       "fail")) < 0) {
            set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_call_method");
            goto cleanup;
        }
    }
    else if (!strcmp (active_state, "failed")) {
        /* This is loosely the equivalent of:
         *
         * systemctl reset-failed --user <unitname>.service
         */
        if ((ret = sd_bus_call_method (sdp->bus,
                                       "org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "ResetFailedUnit",
                                       &error,
                                       NULL,
                                       "s",
                                       sdp->service_name)) < 0) {
            set_errno_log_errmsg (sdp->h, ret, &error, "sd_bus_call_method");
            goto cleanup;
        }
    }
    else if (!strcmp (active_state, "inactive")) {
        /* After cleanup, state could be inactive, but could also be
         * inactive via transitioning states.  AFAICT, no way to tell
         * difference.
         *
         * Without any better clue of what to do, we return EPERM.
         */
        errno = EPERM;
        goto cleanup;
    }
    else {
        /* Assumption: All other states "activating",
         * "deactivating", "reloaded", we are transitioning to a final
         * state or "active" (success w/ RemainAfterExit) or
         * "failed" */
        sdp_log (sdp->h,
                 LOG_DEBUG,
                 "Cleanup on ActiveState=%s",
                 active_state);
        errno = EAGAIN;
        goto cleanup;
    }

    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_error_free (&error);
    free (active_state);
    errno = save_errno;
    return rv;
}

int sdprocess_list (flux_t *h,
                    const char *pattern,
                    sdprocess_list_f list_cb,
                    void *arg)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;
    char *patternv[2] = {0};
    char **patternvptr = NULL;
    int save_errno, ret, rv = -1;

    if (!list_cb) {
        errno = EINVAL;
        return -1;
    }

    if ((ret = sd_bus_default_user (&bus)) < 0) {
        errno = -ret;
        return -1;
    }

    if ((ret = sd_bus_message_new_method_call (
                                         bus,
                                         &m,
                                         "org.freedesktop.systemd1",
                                         "/org/freedesktop/systemd1",
                                         "org.freedesktop.systemd1.Manager",
                                         "ListUnitsByPatterns")) < 0) {
        set_errno_log (h, ret, "sd_bus_message_new_method_call");
        goto cleanup;
    }

    /* states */
    if ((ret = sd_bus_message_append_strv (m, NULL))) {
        set_errno_log (h, ret, "sd_bus_message_append_strv");
        goto cleanup;
    }

    if (pattern) {
        patternv[0] = (char *)pattern;
        patternv[1] = NULL;
        patternvptr = patternv;
    }

    /* patterns */
    if ((ret = sd_bus_message_append_strv (m, patternvptr)) < 0) {
        set_errno_log (h, ret, "sd_bus_message_append_strv");
        goto cleanup;
    }

    if ((ret = sd_bus_call (bus, m, 0, &error, &reply)) < 0) {
        set_errno_log_errmsg (h, ret, &error, "sd_bus_call");
        goto cleanup;
    }

    if ((ret = sd_bus_message_enter_container (reply,
                                               SD_BUS_TYPE_ARRAY,
                                               "(ssssssouso)")) < 0) {
        set_errno_log (h, ret, "sd_bus_message_enter_container");
        goto cleanup;
    }

    while (1) {
        const char *unitname;
        int cb_ret;

        if ((ret = sd_bus_message_read (reply,
                                        "(ssssssouso)",
                                        &unitname,
                                        NULL, /* description */
                                        NULL, /* load state */
                                        NULL, /* active state */
                                        NULL, /* sub state */
                                        NULL, /* following */
                                        NULL, /* unit path */
                                        NULL, /* job id */
                                        NULL, /* job type */
                                        NULL  /* job_path */
                                        )) < 0) {
            set_errno_log (h, ret, "sd_bus_message_read");
            goto cleanup;
        }

        if (ret == 0)
            break;

        if ((cb_ret = list_cb (h, unitname, arg)) < 0)
            goto cleanup;
        /* don't call 'break', sd_bus_message_exit_container() will
         * fail if we're not done iterating */
        if (cb_ret > 0)
            goto out;
    }

    if ((ret = sd_bus_message_exit_container (reply)) < 0) {
        set_errno_log (h, ret, "sd_bus_message_exit_container");
        goto cleanup;
    }

out:
    rv = 0;
cleanup:
    save_errno = errno;
    sd_bus_message_unref (m);
    sd_bus_message_unref (reply);
    sd_bus_unref (bus);
    sd_bus_error_free (&error);
    errno = save_errno;
    return rv;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
