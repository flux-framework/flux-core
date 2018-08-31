#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"

int main(int argc, char** argv)
{
    const char *tmp = getenv ("TMPDIR");
    char path[PATH_MAX];
    char path2[PATH_MAX];
    int n;
    struct stat sb;
    int fd, len;

    plan (NO_PLAN);

    len = snprintf (path, sizeof (path), "%s/unlink_test.XXXXXX", tmp ? tmp : "/tmp");
    if ((len < 0) || (len >= sizeof (path)) || !mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");

    n = unlink_recursive (path);
    errno = 0;
    ok (n == 1 && stat (path, &sb) < 0 && errno == ENOENT,
        "cleaned up directory containing nothing");


    len = snprintf (path, sizeof (path), "%s/unlink_test.XXXXXX", tmp ? tmp : "/tmp");
    if ((len < 0) || (len >= sizeof (path)) || !mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    len = snprintf (path2, sizeof (path2), "%s/a", path);
    if ((len < 0) || (len >= sizeof (path2)) || mkdir (path2, 0777) < 0)
        BAIL_OUT ("could not create subdirectory");
    n = unlink_recursive (path);
    errno = 0;
    ok (n == 2 && stat (path, &sb) < 0 && errno == ENOENT,
        "cleaned up directory containing 1 dir");


    len = snprintf (path, sizeof (path), "%s/unlink_test.XXXXXX", tmp ? tmp : "/tmp");
    if ((len < 0) || (len >= sizeof (path)) || !mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    len = snprintf (path2, sizeof (path2), "%s/a", path);
    if ((len < 0) || (len >= sizeof (path2)) || mkdir (path2, 0777) < 0)
        BAIL_OUT ("could not create subdirectory");
    len = snprintf (path2, sizeof (path2), "%s/b", path);
    if ((len < 0) || (len >= sizeof (path2))
        || (fd = open (path2, O_CREAT | O_RDWR, 0666)) < 0 || close (fd) < 0)
        BAIL_OUT ("could not create subdirectory");
    n = unlink_recursive (path);
    errno = 0;
    ok (n == 3 && stat (path, &sb) < 0 && errno == ENOENT,
        "cleaned up directory containing 1 dir (empty) + 1 file ");

    len = snprintf (path, sizeof (path), "%s/unlink_test.XXXXXX", tmp ? tmp : "/tmp");
    if ((len < 0) || (len >= sizeof (path)) || !mkdtemp (path))
        BAIL_OUT ("could not create tmp directory");
    len = snprintf (path2, sizeof (path2), "%s/a", path);
    if ((len < 0) || (len >= sizeof (path2)) || mkdir (path2, 0777) < 0)
        BAIL_OUT ("could not create subdirectory");
    len = snprintf (path2, sizeof (path2), "%s/b", path);
    if ((len < 0) || (len >= sizeof (path2))
        || (fd = open (path2, O_CREAT | O_RDWR, 0666)) < 0 || close (fd) < 0)
        BAIL_OUT ("could not create subdirectory");
    len = snprintf (path2, sizeof (path2), "%s/a/a", path);
    if ((len < 0) || (len >= sizeof (path2))
       || (fd = open (path2, O_CREAT | O_RDWR, 0666)) < 0 || close (fd) < 0)
        BAIL_OUT ("could not create file in subdirectory");
    n = unlink_recursive (path);
    errno = 0;
    ok (n == 4 && stat (path, &sb) < 0 && errno == ENOENT,
        "cleaned up directory containing 1 dir (with 1 file) + 1 file ");

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
