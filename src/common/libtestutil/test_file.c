#include "test_file.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "src/common/libtap/tap.h"


void
create_test_file (const char *dir,
                  const char *prefix,
                  const char *extension,
                  char *path,
                  size_t pathlen,
                  const char *contents)
{
    int fd;
    const char *ext = extension ? extension : ".txt";
    if (snprintf (path,
                  pathlen,
                  "%s/%s.XXXXXX.%s",
                  dir ? dir : "/tmp",
                  prefix,
                  ext) >= pathlen)
        BAIL_OUT ("snprintf overflow");
    fd = mkstemps (path, 5);
    if (fd < 0)
        BAIL_OUT ("mkstemp %s: %s", path, strerror (errno));
    if (write (fd, contents, strlen (contents)) != strlen (contents))
        BAIL_OUT ("write %s: %s", path, strerror (errno));
    if (close (fd) < 0)
        BAIL_OUT ("close %s: %s", path, strerror (errno));
}

void
create_test_dir (char *dir, int dirlen)
{
    const char *tmpdir = getenv ("TMPDIR");
    if (snprintf (dir,
                  dirlen,
                  "%s/cf.XXXXXXX",
                  tmpdir ? tmpdir : "/tmp") >= dirlen)
        BAIL_OUT ("snprintf overflow");
    if (!mkdtemp (dir))
        BAIL_OUT ("mkdtemp %s: %s", dir, strerror (errno));
}

static pthread_once_t global_test_dir_once = PTHREAD_ONCE_INIT;
static char global_test_dir[PATH_MAX];
static void
init_global_test_dir ()
{
  create_test_dir (global_test_dir, sizeof global_test_dir);
}

__attribute__ ((destructor))
static void
cleanup_global_test_dir ()
{
  // Skip if it was never created
  if (global_test_dir[0] == 0)
    return;
  if (rmdir (global_test_dir) < 0) {
    fprintf (stderr, "could not cleanup test dir %s: %s\n", global_test_dir, strerror (errno));
    exit (1);
  }
}

const char *
get_test_dir ()
{
  pthread_once (&global_test_dir_once, init_global_test_dir);
  return global_test_dir;
}
