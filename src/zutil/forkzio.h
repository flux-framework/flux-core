typedef struct forkzio_handle_struct *forkzio_t;

enum {
    FORKZIO_FLAG_PTY = 1,
    FORKZIO_FLAG_DEBUG = 2,
};

/* Spawn a thread which then forks and execs the command defined by (ac, av).
 * The thread runs a zloop which copies data arriving from the command's
 * stdout and stderr pipes to a zmq PAIR socket, and from the PAIR socket to
 * the commands stdin pipe.  Data is encapsulated in JSON using zio.
 * Returns a handle for the running process or NULL on failure.
 */
forkzio_t forkzio_open (zctx_t *zctx, int ac, char **av, int flags);

/* Destroy a forkzio handle, closing the zmq PAIR socket.
 */
void      forkzio_close (forkzio_t fh);

/* Retrieve the "parent" end of the PAIR socket.
 */
void     *forkzio_get_zsocket (forkzio_t fh);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
