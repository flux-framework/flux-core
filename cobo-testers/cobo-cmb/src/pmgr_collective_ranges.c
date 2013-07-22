#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pmgr_collective_common.h"

/* TODO: add functions to parse and free range resources */

#define PMGR_RANGE_FIND  (0)
#define PMGR_RANGE_COUNT (1)

static int pmgr_range_step_numbers(const char* range, int flag, int* n, char* target, size_t target_len)
{
    /* only return success on find if we find the target */
    int rc = PMGR_SUCCESS;
    if (flag == PMGR_RANGE_FIND) {
        rc = PMGR_FAILURE;
    }

    /* split range into high and low numbers, one-by-one */
    char* low  = strdup(range);
    char* high = strdup(range);
    if (low == NULL || high == NULL) {
        if (low != NULL) {
            free(low);
        }
        if (high != NULL) {
            free(high);
        }
        return PMGR_FAILURE;
    }

    const char* p = range;
    while ((flag == PMGR_RANGE_COUNT || (flag == PMGR_RANGE_FIND && *n > 0)) && *p != '\0') {
        /* skip past any commas */
        while (*p == ',') {
            p++;
        }

        /* pull off low value */
        int low_count = 0;
        while (*p >= '0' && *p <= '9') {
            low[low_count] = *p;
            low_count++;
            p++;
        }
        low[low_count] ='\0';

        /* check that we got at least one digit */
        if (low_count == 0) {
            /* if we didn't get a digit, and if we're not at the end of the list, print an error */
            if (*p != '\0') {
                pmgr_error("Ill-formed range in %s @ %s:%d",
                    range, __FILE__, __LINE__
                );
                rc = PMGR_FAILURE;
            }
            break;
        }

        /* get the high value */
        if (*p == '-') {
            /* got a low-high range like 3-7 */
            /* skip past the - separator */
            p++;

            /* pull off the high count */
            int high_count = 0;
            while (*p >= '0' && *p <= '9') {
                high[high_count] = *p;
                high_count++;
                p++;
            }
            high[high_count] ='\0';

            /* check that we got at least one digit */
            if (high_count == 0) {
                pmgr_error("Ill-formed low-high range in %s @ %s:%d",
                    range, __FILE__, __LINE__
                );
                rc = PMGR_FAILURE;
                break;
            }
        } else if (*p == ',' || *p == '\0') {
            /* there is no high count, so set high = low */
            strcpy(high, low);
        } else {
            /* found bad character */
            pmgr_error("Ill-formed range in %s @ %s:%d",
                range, __FILE__, __LINE__
            );
            rc = PMGR_FAILURE;
            break;
        }

        int i;
        int low_value  = atoi(low);
        int high_value = atoi(high);
        for (i = low_value; i <= high_value; i++) {
            /* increment our running count by one */
            if (flag == PMGR_RANGE_COUNT) {
                (*n)++;
            } else {
                /* if we hit our target, build the name */
                (*n)--;
                if (*n == 0) {
                    /* construct the node name */
                    /* TODO: use same number of sig digits as low and high values */
                    if (snprintf(target, target_len, "%d", i) >= target_len) {
                        /* error */
                        pmgr_error("Failed to allocate memory to build nodename @ %s:%d",
                            __FILE__, __LINE__
                        );
                        rc = PMGR_FAILURE;
                        break;
                    } else {
                        rc = PMGR_SUCCESS;
                    }
                }
            }
        }
    }

    free(low);
    free(high);

    return rc;
}

static int pmgr_range_step_nodelist(const char* nodelist, int flag, int* n, char* target, size_t target_len)
{
    /* only return success on find if we find the target */
    int rc = PMGR_SUCCESS;
    if (flag == PMGR_RANGE_FIND) {
        rc = PMGR_FAILURE;
    }

    /* allocate buffers to split the nodelist into hostname and range components */
    char* hostname = strdup(nodelist);
    char* range    = strdup(nodelist);
    if (hostname == NULL || range == NULL) {
        if (hostname != NULL) {
            free(hostname);
        }
        if (range != NULL) {
            free(range);
        }
        return PMGR_FAILURE;
    }

    /* step through the characters of the nodelist */
    const char* p = nodelist;
    while ((flag == PMGR_RANGE_COUNT || (flag == PMGR_RANGE_FIND && *n > 0)) && *p != '\0') {
        /* skip past any commas */
        while (*p == ',') {
            p++;
        }

        /* pull off hostname */
        int hostname_count = 0;
        while (! ((*p >= '0' && *p <= '9') || *p == '[' || *p == '\0')) {
            hostname[hostname_count] = *p;
            hostname_count++;
            p++;
        }
        hostname[hostname_count] ='\0';

        /* pull off the range */
        if (*p >= '0' && *p <= '9') {
            /* hostname followed by a single number: atlas37 */
            int range_count = 0;
            while (*p >= '0' && *p <= '9') {
                range[range_count] = *p;
                range_count++;
                p++;
            }
            range[range_count] ='\0';
        } else if (*p == '[') {
            /* hostname followed by set of brackets: atlas[37-39,43,...] */
            /* skip past the [ bracket */
            p++;

            /* the range is everything we find until we hit the closing ] bracket */
            int range_count = 0;
            while (*p != ']' && *p != '\0') {
                range[range_count] = *p;
                range_count++;
                p++;
            }
            range[range_count] ='\0';

            /* skip past the ] bracket */
            if (*p == ']') {
                p++;
            } else {
                /* ill-formed range, missing right bracket */
                pmgr_error("Mising ']' in %s @ %s:%d",
                    nodelist, __FILE__, __LINE__
                );
                rc = PMGR_FAILURE;
                break;
            }
        } else {
            /* ill-formed range */
            pmgr_error("Mising node numbers in %s @ %s:%d",
                nodelist, __FILE__, __LINE__
            );
            rc = PMGR_FAILURE;
            break;
        }

        /* allocate space to write node target into in case we find it */
        char* node_target = strdup(range);
        if (node_target == NULL) {
            /* error */
            pmgr_error("Failed to allocate memory to build nodename @ %s:%d",
                __FILE__, __LINE__
            );
            rc = PMGR_FAILURE;
            break;
        }

        /* now step through node range */
        size_t node_target_len = strlen(node_target) + 1;
        if (pmgr_range_step_numbers(range, flag, n, node_target, node_target_len) == PMGR_SUCCESS) {
            if (flag == PMGR_RANGE_FIND) {
                /* found it, let's build the hostname */

                /* determine how much space we need to allocate */
                int target_len_actual = strlen(hostname) + strlen(node_target) + 1;

                /* allocate space */
                if (target_len_actual > target_len) {
                    /* error */
                    pmgr_error("Insufficient space to write target name of %d bytes @ %s:%d",
                        target_len_actual, __FILE__, __LINE__
                    );
                    free(node_target);
                    node_target = NULL;
                    rc = PMGR_FAILURE;
                    break;
                }

                /* construct the node name */
                /* TODO: use same number of sig digits as low and high values */
                snprintf(target, target_len, "%s%s", hostname, node_target);
                rc = PMGR_SUCCESS;
            }
        } else {
#if 0
            /* detected an invalid range */
            pmgr_error("Invalid node range %s @ %s:%d",
                nodelist, __FILE__, __LINE__
            );
            free(node_target);
            node_target = NULL;
            rc = PMGR_FAILURE;
            break;
#endif
        }

        /* free the memory we allocated to store the node number */
        if (node_target != NULL) {
            free(node_target);
            node_target = NULL;
        }
    }

    free(range);
    free(hostname);

    return rc;
}

int pmgr_range_numbers_size(const char* range, int* size)
{
    *size = 0;
    if (pmgr_range_step_numbers(range, PMGR_RANGE_COUNT, size, NULL, 0) == PMGR_SUCCESS) {
        return PMGR_SUCCESS;
    }
    return PMGR_FAILURE;
}

int pmgr_range_numbers_nth(const char* range, int n, char* target, size_t target_len)
{
    *target = '\0';
    if (pmgr_range_step_numbers(range, PMGR_RANGE_FIND, &n, target, target_len) == PMGR_SUCCESS) {
        return PMGR_SUCCESS;
    }
    return PMGR_FAILURE;
}

int pmgr_range_nodelist_size(const char* nodelist, int* size)
{
    *size = 0;
    if (pmgr_range_step_nodelist(nodelist, PMGR_RANGE_COUNT, size, NULL, 0) == PMGR_SUCCESS) {
        return PMGR_SUCCESS;
    }
    return PMGR_FAILURE;
}

int pmgr_range_nodelist_nth(const char* nodelist, int n, char* target, size_t target_len)
{
    *target = '\0';
    if (pmgr_range_step_nodelist(nodelist, PMGR_RANGE_FIND, &n, target, target_len) == PMGR_SUCCESS) {
        return PMGR_SUCCESS;
    }
    return PMGR_FAILURE;
}
