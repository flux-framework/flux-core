#include <stdbool.h>

/* Create a KVS dirent.
 * 'type' is one of { "FILEREF", "DIRREF", "FILEVAL", "DIRVAL", "LINKVAL" }.
 * 'arg' is dependent on the type.  This function asserts on failure.
 */
json_object *dirent_create (char *type, void *arg);

/* Append a JSON object containing
 *     { "key" : key, "dirent" : dirent }
 *     { "key" : key, "dirent" : nil }
 * to a json array, creating '*array' if necessary.  This is used to build
 * a KVS commit, where each new object is an ordered operation that adds/
 * changes/unlinks a key in KVS namespace.  This function asserts on failure.
 */
void dirent_append (json_object **array, const char *key, json_object *dirent);

/* Compare two dirents.
 * N.B. The serialize/strcmp method used here can return false negatives,
 * but a positive can be relied on.
 */
bool dirent_match (json_object *dirent1, json_object *dirent2);

int dirent_validate (json_object *dirent);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
