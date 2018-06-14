#ifndef _EV_BUFFER_WRITE_H
#define _EV_BUFFER_WRITE_H

#include "src/common/libev/ev.h"
#include "src/common/libflux/buffer.h"

struct ev_buffer_write;

typedef void (*ev_buffer_write_f)(struct ev_loop *loop,
                                  struct ev_buffer_write *ebw,
                                  int revents);

struct ev_buffer_write {
    ev_io             io_w;
    int               fd;
    ev_buffer_write_f cb;
    flux_buffer_t     *fb;
    struct ev_loop    *loop;
    bool              start;    /* flag, if user started reactor */
    bool              eof;      /* flag, eof written             */
    bool              closed;   /* flag, fd has been closed      */
    int               close_errno;  /* errno from close          */
    void              *data;
};

int ev_buffer_write_init (struct ev_buffer_write *ebw,
                          int fd,
                          int size,
                          ev_buffer_write_f cb,
                          struct ev_loop *loop);
void ev_buffer_write_cleanup (struct ev_buffer_write *ebw);
void ev_buffer_write_start (struct ev_loop *loop, struct ev_buffer_write *ebw);
void ev_buffer_write_stop (struct ev_loop *loop, struct ev_buffer_write *ebw);
void ev_buffer_write_wakeup (struct ev_buffer_write *ebw);
#endif /* !_EV_BUFFER_WRITE_H */
