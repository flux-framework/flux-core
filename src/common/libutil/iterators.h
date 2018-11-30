#ifndef FLUX_ITERATORS_H
#define FLUX_ITERATORS_H

#define FOREACH_ZLIST(LIST, VAR)   \
    for((VAR) = zlist_first(LIST); \
            VAR;                   \
            (VAR) = zlist_next(LIST))

#define FOREACH_ZLISTX(LIST, VAR)   \
    for((VAR) = zlistx_first(LIST); \
            VAR;                   \
            (VAR) = zlistx_next(LIST))

#define FOREACH_ZHASH(HASH, KEY, VALUE) \
    for((VALUE) = zhash_first(HASH),    \
        (KEY) = zhash_cursor(HASH);     \
        (VALUE) && (KEY);               \
        (VALUE) = zhash_next(HASH),     \
        (KEY) = zhash_cursor(HASH))

#define FOREACH_ZHASHX(HASH, KEY, VALUE) \
    for((VALUE) = zhashx_first(HASH),    \
        (KEY) = zhashx_cursor(HASH);     \
        (VALUE) && (KEY);               \
        (VALUE) = zhashx_next(HASH),     \
        (KEY) = zhashx_cursor(HASH))

#endif /* FLUX_ITERATORS_H */
