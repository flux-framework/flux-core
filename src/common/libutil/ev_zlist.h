#ifndef _EV_ZLIST_H
#define _EV_ZLIST_H

typedef struct ev_list_struct ev_zlist;
typedef void (*ev_zlist_cb)(struct ev_loop *loop, ev_zlist *w, int revents);

struct ev_list_struct {
    ev_prepare  prepare_w;
    ev_idle     idle_w;
    ev_check    check_w;
    zlist_t     *zlist;
    int         events;
    ev_zlist_cb cb;
    void        *data;
};

int ev_zlist_init (ev_zlist *w, ev_zlist_cb cb, zlist_t *zlist, int events);
void ev_zlist_start (struct ev_loop *loop, ev_zlist *w);
void ev_zlist_stop (struct ev_loop *loop, ev_zlist *w);

#endif /* !_EV_ZLIST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

