#ifndef _UTIL_GETRUSAGE_JSON_H
#define _UTIL_GETRUSAGE_JSON_H

#include <sys/time.h>
#include <sys/resource.h>
#include "src/common/libjson-c/json.h"

int getrusage_json (int who, json_object **ru);

#endif /* !UTIL_GETRUSAGE_JSON_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
