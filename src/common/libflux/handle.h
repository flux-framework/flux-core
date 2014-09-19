#ifndef _FLUX_CORE_HANDLE_H
#define _FLUX_CORE_HANDLE_H

typedef struct flux_handle_struct *flux_t;

/* Flags for handle creation and flux_flags_set()/flux_flags_unset.
 */
enum {
    FLUX_FLAGS_TRACE = 1,   /* print 0MQ messages sent over the flux_t */
                            /*   handle on stdout. */
};

/* A mechanism is provide for users to attach auxiliary state to the flux_t
 * handle by name.  The FluxFreeFn, if non-NULL, will be called
 * to destroy this state when the handle is destroyed.
 */
typedef void (*FluxFreeFn)(void *arg);
void *flux_aux_get (flux_t h, const char *name);
void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn destroy);

/* Set/clear FLUX_FLAGS_* on a flux_t handle.
 */
void flux_flags_set (flux_t h, int flags);
void flux_flags_unset (flux_t h, int flags);

#endif /* !_FLUX_CORE_HANDLE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
