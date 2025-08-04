#include "test_file.h"

#include <errno.h>
#include <string.h>


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
