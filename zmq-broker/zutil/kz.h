#ifndef HAVE_FLUX_KZ_H
#define HAVE_FLUX_KZ_H

typedef struct kz_struct *kz_t;

typedef void (*kz_ready_f) (kz_t kz, void *arg);

enum {
    /* mode */
    KZ_FLAGS_READ =     0x0001,
    KZ_FLAGS_WRITE =    0x0002,
    KZ_FLAGS_MODEMASK = 0x0003,

    /* general */
    KZ_FLAGS_NONBLOCK = 0x0010,

    /* write flags */
    KZ_FLAGS_APPEND =   0x0100, /* not supported yet */
    KZ_FLAGS_TRUNC  =   0x0200,
};    

kz_t kz_open (flux_t h, const char *name, int flags);
int kz_put (kz_t kz, char *data, int len);
int kz_get (kz_t kz, char **datap);
int kz_flush (kz_t kz);
int kz_close (kz_t kz);

/* Call kz_open with KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK, then
 * register your read_cb () here.  Your function should call
 * kz_get().
 */
int kz_set_ready_cb (kz_t kz, kz_ready_f ready_cb, void *arg);

#endif /* !HAVE_FLUX_KZ_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
