//flux_msg_handler_f
extern "Python" void message_handler_wrapper(flux_t *, flux_msg_handler_t *, const flux_msg_t *, void *);

//flux_watcher_f
extern "Python" void timeout_handler_wrapper(flux_reactor_t *, flux_watcher_t *, int, void *);
extern "Python" void fd_handler_wrapper(flux_reactor_t *, flux_watcher_t *, int, void *);
extern "Python" void signal_handler_wrapper(flux_reactor_t *, flux_watcher_t *, int, void *);

//flux_continuation_f
extern "Python" void continuation_callback(flux_future_t *, void *);

//flux_future_init_f
extern "Python" void init_callback(flux_future_t *, void *);
