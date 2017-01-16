#include "arp.h"

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

pthread_once_t arp_stack_once = PTHREAD_ONCE_INIT;
pthread_key_t arp_stack;

static void arp_stack_drain ()
{
    arp_scope_pop (0);
}

static void arp_stack_init ()
{
    pthread_key_create (&arp_stack, arp_stack_drain);
}

struct arp_pair {
    arp_cb_f fn;
    void *obj;
};

struct arp_autorelease_pool {
    struct arp_pair *stack;
    size_t top;
    size_t cap;
};

ssize_t arp_scope_push ()
{
    pthread_once (&arp_stack_once, arp_stack_init);
    struct arp_autorelease_pool *pool = pthread_getspecific (arp_stack);
    if (pool == NULL) {
        pool = calloc (1, sizeof *pool);
        pthread_setspecific (arp_stack, pool);
    }
    arp_auto_call ((void*)7, (void*)7);
    return pool->top - 1;
}
static ssize_t arp_scope_pop_inner (ssize_t scope, int stop_at_one)
{
    struct arp_autorelease_pool *pool = pthread_getspecific (arp_stack);
    assert (pool && (scope >= 0));
    ssize_t top = 0;
    for (top = pool->top - 1; top >= scope && top >= 0; top--) {
        if (pool->stack[top].fn == (void*)7) {
            if (stop_at_one)
                break;
            else
                continue;
        }
        if (!pool->stack[top].fn)
            fop_release (pool->stack[top].obj);
        else
            pool->stack[top].fn (pool->stack[top].obj);
    }
    pool->top = top;
    return top;
}
ssize_t arp_scope_pop (ssize_t scope)
{
    return arp_scope_pop_inner (scope, 0);
}
ssize_t arp_scope_pop_one ()
{
    return arp_scope_pop_inner (0, 1);
}

void *arp_auto_call (void *o, arp_cb_f fn)
{
    struct arp_autorelease_pool *pool = pthread_getspecific (arp_stack);
    assert (pool);
    if (pool->top >= pool->cap) {
        // Need more space
        size_t new_cap = pool->cap ? pool->cap * 2 : 512;
        void *tmp = realloc (pool->stack, new_cap);
        assert (tmp);
        pool->stack = tmp;
        pool->cap = new_cap;
    }
    pool->stack[pool->top].obj = o;
    pool->stack[pool->top].fn = fn;
    pool->top++;
    return o;
}

fop *arp_autorelease (fop *o)
{
    return arp_auto_call (o, NULL);
}
