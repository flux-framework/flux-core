/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/*! \file usa.h
 *  \brief Unique String Arrays
 *
 *  A unique_string_array_t stores a dense array of strings that can be easily
 *  generated from output that must be split, joined, and otherwise
 *  manipulated.  This is most useful for generating argv-style argument lists
 *  and managing paths where input path components may not have been processed
 *  into individual path components ahead of time.
 *
 *  All strings used herein are sds-based, and can be treated as such with
 *  confidence.  The parameter types do not reflect this for interoperability
 *  with other code.
 */

#ifndef FLUX_UNIQUE_STRING_ARRAY_H
#define FLUX_UNIQUE_STRING_ARRAY_H

#include <stdbool.h>

#include "sds.h"
#include "vec.h"

/**
 * @brief Base structure for a usa, may be stack-based, but needs init.
 */
typedef struct unique_string_array
{
    sds sep;
    vec_str_t components;
    sds string_cache;
    bool clean;
} unique_string_array_t;

/**
 * @brief Get the length of the array in number of strings.
 *
 * @param a The array
 *
 * @return The number of strings in the array
 */
#define usa_length(a) ((a)->components.length)

/**
 * @brief Get the underlying array, usable as an argv-style parameter.
 *
 * @param a The array class
 *
 * @return The underlying C array
 */
#define usa_data(a) ((a)->components.data)

/**
 * @brief Allocate, but do not initialize, a usa object.
 *
 * @return An un-initialized usa
 */
unique_string_array_t *usa_alloc ();
/**
 * @brief Initialize a usa, most useful for stack-allocated objects.
 *
 * @param item Pointer to the usa to initialize
 */
void usa_init (unique_string_array_t *item);
/**
 * @brief Allocate and initialize a new usa in one step.
 *
 * @return A ready-to-use usa instance, NULL on failure
 */
unique_string_array_t *usa_new ();
/**
 * @brief Re-set a usa as though it were new, keeping the storage active.
 *
 * @param item The usa to clear
 */
void usa_clear (unique_string_array_t *item);
/**
 * @brief Clear and deallocate the storage used by a usa.
 *
 * @param item The usa to be destroyed
 */
void usa_destroy (unique_string_array_t *item);

/**
 * @brief Set a usa to contain only the string "str," clearing the current
 * contents, does not split its argument by separator.
 *
 * @param item The usa to set
 * @param str The string to add
 */
void usa_set (unique_string_array_t *item, const char *str);
/**
 * @brief Push a new string onto the front of the array, requires a move of
 * all other elements, not O(1) and does *not* deduplicate.
 *
 * @param item The usa to push into
 * @param str The string to push
 */
void usa_push (unique_string_array_t *item, const char *str);
/**
 * @brief Push a new string onto the back of the array, this is O(1) unless
 * an allocation is required and does *not* deduplicate.
 *
 * @param item The usa to push into
 * @param str The string to push
 */
void usa_push_back (unique_string_array_t *item, const char *str);
/**
 * @brief Find and remove a string.
 *
 * @param item The usa to remove the string from
 * @param str The string to remove
 *
 * @return On success, the index that was removed, on failure -1
 */
int usa_remove (unique_string_array_t *item, const char *str);

/**
 * @brief Set the separator character to be used by the split and join
 * functions on this object.
 *
 * @param item The array object
 * @param separator The separator to use
 */
void usa_set_separator (unique_string_array_t *item, const char *separator);
/**
 * @brief Split the input string by separator and set the contents of the
 * array to the resulting tokens.
 *
 * This function performs de-duplication as it adds componenets to the array.
 *
 * @param item The array to act on
 * @param str The string to be split and set
 */
void usa_split_and_set (unique_string_array_t *item, const char *str);
/**
 * @brief Split the input string by separator and push the resulting tokens
 * onto the list at front or back.
 *
 * This function performs de-duplication as it adds componenets to the array.
 *
 * @param item The array to act on
 * @param str The string to be split and pushed
 * @param before Push to the front if true, back if false
 */
void usa_split_and_push (unique_string_array_t *item,
                         const char *value,
                         bool before);
/**
 * @brief Get the result of joining all strings in the array, in order, by the
 * separator character.
 *
 * The returned value is owned by the array, and should not be freed. It is a
 * cached value, so repeated calls to this function are cheap if no
 * modifications are made inbetween.
 *
 * @param item The array to join.
 *
 * @return The joined string
 */
const char *usa_get_joined (unique_string_array_t *item);
/**
 * @brief Find a string in the array matching str, if no such match is found
 * return NULL.
 *
 * @param item The array to search
 * @param str The string to search for
 *
 * @return A pointer to the first match in the array or NULL
 */
const char *usa_find (unique_string_array_t *item, const char *str);
/**
 * @brief Find a string in the array matching str and return the index.
 *
 * @param item The array to search
 * @param str The string to search for
 *
 * @return The array index of the first match or -1 if none is found
 */
int usa_find_idx (unique_string_array_t *item, const char *str);

/**
 * @brief Removes all duplicates in the array, leaving the left-most version
 * of each in place.
 *
 * @param item The array to de-duplicate
 */
void usa_deduplicate (unique_string_array_t *item);

#endif /* FLUX_UNIQUE_STRING_ARRAY_H */
