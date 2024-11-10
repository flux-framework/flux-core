/*
 * Copyright (c) 2009-2016 Petri Lehtinen <petri@digip.org>
 *
 * Jansson is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <jansson.h>

/* Taken from jansson v2.13 source. This version does not do a loop
 * check as is done in later versions of this function. The loop check
 * requires access to jansson internals.
 */
int json_object_update_recursive (json_t *object, json_t *other)
{
    const char *key;
    json_t *value;

    if (!json_is_object (object) || !json_is_object (other))
        return -1;

    json_object_foreach (other, key, value) {
        json_t *v = json_object_get (object, key);

        if (json_is_object (v) && json_is_object (value))
            json_object_update_recursive (v, value);
        else
            json_object_set_nocheck (object, key, value);
    }

    return 0;
}
