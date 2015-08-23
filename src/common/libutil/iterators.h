#ifndef FLUX_ITERATORS_H
#define FLUX_ITERATORS_H

#define FOREACH_ZLIST(LIST, VAR)   \
    for((VAR) = zlist_first(LIST); \
            VAR;                   \
            (VAR) = zlist_next(LIST))

#define FOREACH_ZHASH_KEYS(HASH, KEY) \
    FOREACH_ZLIST(zhash_keys(HASH), KEY)

#define FOREACH_ZHASH(HASH, KEY, VALUE) \
    for((VALUE) = zhash_first(HASH),    \
        (KEY) = zhash_cursor(HASH);     \
        (VALUE) && (KEY);               \
        (VALUE) = zhash_next(HASH),     \
        (KEY) = zhash_cursor(HASH))

#endif /* FLUX_ITERATORS_H */
