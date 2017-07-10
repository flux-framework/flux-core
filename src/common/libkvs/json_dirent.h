#include <stdbool.h>

/* JSON directory object:
 * list of key-value pairs where key is a name, value is a dirent
 *
 * JSON dirent objects:
 * object containing one key-value pair where key is one of
 * "FILEREF", "DIRREF", "FILEVAL", "DIRVAL", "LINKVAL", and value is a
 * blobref key into ctx->store (FILEREF, DIRREF), an actual directory, file
 * (value), or link target JSON object (FILEVAL, DIRVAL, LINKVAL).
 *
 * For example, consider KVS containing:
 * a="foo"
 * b="bar"
 * c.d="baz"
 * X -> c.d
 *
 * Root directory:
 * {"a":{"FILEREF":"f1d2d2f924e986ac86fdf7b36c94bcdf32beec15"},
 *  "b":{"FILEREF","8714e0ef31edb00e33683f575274379955b3526c"},
 *  "c":{"DIRREF","6eadd3a778e410597c85d74c287a57ad66071a45"},
 *  "X":{"LINKVAL","c.d"}}
 *
 * Deep copy of root directory:
 * {"a":{"FILEVAL":"foo"},
 *  "b":{"FILEVAL","bar"},
 *  "c":{"DIRVAL",{"d":{"FILEVAL":"baz"}}},
 *  "X":{"LINKVAL","c.d"}}
 *
 * On LINKVAL's:
 * - target is always fully qualified key name
 * - links are always followed in path traversal of intermediate directories
 * - for kvs_get, terminal links are only followed if 'readlink' flag is set
 * - for kvs_put, terminal links are never followed
 */

/* Create a KVS dirent.
 * 'type' is one of { "FILEREF", "DIRREF", "FILEVAL", "DIRVAL", "LINKVAL" }.
 * 'arg' is dependent on the type.  This function asserts on failure.
 */
json_object *dirent_create (const char *type, void *arg);

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
