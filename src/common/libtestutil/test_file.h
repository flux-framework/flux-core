#ifndef TEST_FILE_H
#define TEST_FILE_H

#include <stddef.h>


/** 
 * @brief Get a single statically managed test directory for this process
 */
const char *
get_test_dir ();

void
create_test_file (const char *dir,
                  const char *prefix,
                  const char *extension,
                  char *path,
                  size_t pathlen,
                  const char *contents);
void
create_test_dir (char *dir, int dirlen);

#endif // TEST_FILE_H
