#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/libutil/cleanup.h"

int main(int argc, char** argv)
{
    const char *tmp = getenv ("TMPDIR");
    char file[PATH_MAX];
    char dir[PATH_MAX];
    char dir2[PATH_MAX];
    struct stat sb;
    int fd;

    plan (NO_PLAN);

    /* Independent file and dir
     */
    snprintf (file, sizeof (file), "%s/cleanup_test.XXXXXX", tmp ? tmp : "/tmp");
    if (!(fd = mkstemp (file)))
        BAIL_OUT ("could not create tmp file");
    close (fd);

    snprintf (dir, sizeof (dir), "%s/cleanup_test.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("could not create tmp directory");

    cleanup_push_string (cleanup_file, file);
    cleanup_push_string (cleanup_directory, dir);
    cleanup_run ();
    ok (stat (file, &sb) < 0 && errno == ENOENT,
        "cleanup removed independent file");
    ok (stat (dir, &sb) < 0 && errno == ENOENT,
        "cleanup removed independent dir");

    /* This time put file inside directory
     */
    snprintf (dir, sizeof (dir), "%s/cleanup_test.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("could not create tmp directory");

    snprintf (file, sizeof (file), "%s/file", dir);
    if (!(fd = open (file, O_CREAT, 0644)))
        BAIL_OUT ("could not create tmp file");
    close (fd);

    cleanup_push_string (cleanup_directory, dir);
    cleanup_push_string (cleanup_file, file);
    cleanup_run ();
    ok (stat (file, &sb) < 0 && errno == ENOENT,
        "cleanup removed file pushed second");
    ok (stat (dir, &sb) < 0 && errno == ENOENT,
        "cleanup removed dir pushed first");

    /* Same but reverse push order
     */
    snprintf (dir, sizeof (dir), "%s/cleanup_test.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("could not create tmp directory");

    snprintf (file, sizeof (file), "%s/file", dir);
    if (!(fd = open (file, O_CREAT, 0644)))
        BAIL_OUT ("could not create tmp file");
    close (fd);

    cleanup_push_string (cleanup_file, file);
    cleanup_push_string (cleanup_directory, dir);
    cleanup_run ();
    ok (stat (dir, &sb) == 0,
        "cleanup failed to remove dir pushed first");
    ok (stat (file, &sb) < 0 && errno == ENOENT,
        "cleanup removed file pushed second (1 deep)");

    (void)unlink (dir);

    /* Same but recursive removal
     */
    snprintf (dir, sizeof (dir), "%s/cleanup_test.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("could not create tmp directory");

    snprintf (file, sizeof (file), "%s/file", dir);
    if (!(fd = open (file, O_CREAT, 0644)))
        BAIL_OUT ("could not create tmp file");
    close (fd);

    cleanup_push_string (cleanup_directory_recursive, dir);
    cleanup_run ();

    ok (stat (file, &sb) < 0 && errno == ENOENT,
        "cleanup removed file not pushed (1 deep)");
    ok (stat (dir, &sb) < 0 && errno == ENOENT,
        "cleanup removed pushed dir recursively");

    /* Try couple levels deep
     */
    snprintf (dir, sizeof (dir), "%s/cleanup_test.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp (dir))
        BAIL_OUT ("could not create tmp directory");
    snprintf (dir2, sizeof (dir), "%s/dir", dir);
    if (mkdir (dir2, 0755) < 0)
        BAIL_OUT ("mkdir failed");

    snprintf (file, sizeof (file), "%s/file", dir2);
    if (!(fd = open (file, O_CREAT, 0644)))
        BAIL_OUT ("could not create tmp file");
    close (fd);

    cleanup_push_string (cleanup_directory_recursive, dir);
    cleanup_run ();

    ok (stat (file, &sb) < 0 && errno == ENOENT,
        "cleanup removed file not pushed (2 deep)");
    ok (stat (dir2, &sb) < 0 && errno == ENOENT,
        "cleanup removed dir not pushed (1 deep)");
    ok (stat (dir, &sb) < 0 && errno == ENOENT,
        "cleanup removed pushed dir recursively");

    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
