#ifndef _UTIL_LIBJANSSON_H
#define _UTIL_LIBJANSSON_H

#include <jansson.h>

struct jansson_struct {
    int         (*vunpack_ex)(json_t *root, json_error_t *error, size_t flags,
                                const char *fmt, va_list ap);
    json_t *    (*vpack_ex)(json_error_t *error, size_t flags,
                                const char *fmt, va_list ap);
    char *      (*dumps)(const json_t *root, size_t flags);
    json_t *    (*loads)(const char *input, size_t flags, json_error_t *error);
    void        (*delete)(json_t *json); /* do not use directly */

    void *dso;
};

void jansson_decref (struct jansson_struct *json, json_t *obj);

struct jansson_struct *jansson_create (void);
void jansson_destroy (struct jansson_struct *js);

#endif /* _UTIL_JANSSON_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
