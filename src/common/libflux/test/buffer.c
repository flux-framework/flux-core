#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "src/common/libflux/buffer.h"
#include "src/common/libtap/tap.h"

#define FLUX_BUFFER_TEST_MAXSIZE 1048576

void basic (void)
{
    flux_buffer_t *fb;
    int pipefds[2];
    char buf[1024];
    const char *ptr;
    int len;

    ok (pipe (pipefds) == 0,
        "pipe succeeded");

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes initially returns 0");

    /* write & peek tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns length of bytes written");

    ok ((ptr = flux_buffer_peek (fb, 2, &len)) != NULL
        && len == 2,
        "flux_buffer_peek with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "flux_buffer_peek returns exepected data");

    ok ((ptr = flux_buffer_peek (fb, -1, &len)) != NULL
        && len == 3,
        "flux_buffer_peek with length -1 works");

    ok (!memcmp (ptr, "foo", 3),
        "flux_buffer_peek returns exepected data");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns unchanged length after peek");

    ok (flux_buffer_drop (fb, 2) == 2,
        "flux_buffer_drop works");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns length of remaining bytes written");

    ok (flux_buffer_drop (fb, -1) == 1,
        "flux_buffer_drop drops remaining bytes");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 with all bytes dropped");

    /* write and read tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns length of bytes written");

    ok ((ptr = flux_buffer_read (fb, 2, &len)) != NULL
        && len == 2,
        "flux_buffer_read with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "flux_buffer_read returns exepected data");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns new length after read");

    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL
        && len == 1,
        "flux_buffer_peek with length -1 works");

    ok (!memcmp (ptr, "o", 1),
        "flux_buffer_peek returns exepected data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 with all bytes read");

    /* write_line & peek_line tests */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    ok (flux_buffer_write_line (fb, "foo") == 4,
        "flux_buffer_write_line works");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_lines (fb) == 1,
        "flux_buffer_lines returns 1 on line written");

    ok ((ptr = flux_buffer_peek_line (fb, &len)) != NULL
        && len == 4,
        "flux_buffer_peek_line works");

    ok (!memcmp (ptr, "foo\n", 4),
        "flux_buffer_peek_line returns exepected data");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns unchanged length after peek_line");

    ok (flux_buffer_drop_line (fb) == 4,
        "flux_buffer_drop_line works");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 after drop_line");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 after drop_line");

    /* write_line & read_line tests */

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 on no line");

    ok (flux_buffer_write_line (fb, "foo") == 4,
        "flux_buffer_write_line works");

    ok (flux_buffer_bytes (fb) == 4,
        "flux_buffer_bytes returns length of bytes written");

    ok (flux_buffer_lines (fb) == 1,
        "flux_buffer_lines returns 1 on line written");

    ok ((ptr = flux_buffer_read_line (fb, &len)) != NULL
        && len == 4,
        "flux_buffer_peek_line works");

    ok (!memcmp (ptr, "foo\n", 4),
        "flux_buffer_peek_line returns exepected data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 after read_line");

    ok (flux_buffer_lines (fb) == 0,
        "flux_buffer_lines returns 0 after read_line");

    /* peek_to_fd tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_peek_to_fd (fb, pipefds[1], 2) == 2,
        "flux_buffer_peek_to_fd specific length works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 2,
        "read correct number of bytes");

    ok (memcmp (buf, "fo", 2) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns correct length after peek");

    ok (flux_buffer_peek_to_fd (fb, pipefds[1], -1) == 3,
        "flux_buffer_peek_to_fd length -1 works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 3,
        "read correct number of bytes");

    ok (memcmp (buf, "foo", 3) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 3,
        "flux_buffer_bytes returns correct length after peek");

    ok (flux_buffer_drop (fb, -1) == 3,
        "flux_buffer_drop drops remaining bytes");

    /* read_to_fd tests */

    ok (flux_buffer_write (fb, "foo", 3) == 3,
        "flux_buffer_write works");

    ok (flux_buffer_read_to_fd (fb, pipefds[1], 2) == 2,
        "flux_buffer_read_to_fd specific length works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 2,
        "read correct number of bytes");

    ok (memcmp (buf, "fo", 2) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns correct length after read");

    ok (flux_buffer_read_to_fd (fb, pipefds[1], -1) == 1,
        "flux_buffer_read_to_fd length -1 works");

    memset (buf, '\0', 1024);
    ok (read (pipefds[0], buf, 1024) == 1,
        "read correct number of bytes");

    ok (memcmp (buf, "o", 1) == 0,
        "read returned correct data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns correct length after read");

    /* write_from_fd and read tests */

    ok (write (pipefds[1], "foo", 3) == 3,
        "write to pipe works");

    ok (flux_buffer_write_from_fd (fb, pipefds[0], -1) == 3,
        "flux_buffer_write_from_fd works");

    ok ((ptr = flux_buffer_read (fb, 2, &len)) != NULL
        && len == 2,
        "flux_buffer_read with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "flux_buffer_read returns exepected data");

    ok (flux_buffer_bytes (fb) == 1,
        "flux_buffer_bytes returns new length after read");

    ok ((ptr = flux_buffer_read (fb, -1, &len)) != NULL
        && len == 1,
        "flux_buffer_peek with length -1 works");

    ok (!memcmp (ptr, "o", 1),
        "flux_buffer_peek returns exepected data");

    ok (flux_buffer_bytes (fb) == 0,
        "flux_buffer_bytes returns 0 with all bytes read");

    flux_buffer_destroy (fb);
    close (pipefds[0]);
    close (pipefds[1]);
}

void corner_case (void)
{
    flux_buffer_t *fb;

    ok (flux_buffer_create (-1) == NULL
        && errno == EINVAL,
        "flux_buffer_create fails on bad input -1");

    ok (flux_buffer_create (0) == NULL
        && errno == EINVAL,
        "flux_buffer_create fails on bad input 0");

    /* all functions fail on NULL fb pointer */
    ok (flux_buffer_bytes (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_bytes fails on NULL pointer");
    ok (flux_buffer_drop (NULL, -1) < 0
        && errno == EINVAL,
        "flux_buffer_drop fails on NULL pointer");
    ok (flux_buffer_peek (NULL, -1, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_peek fails on NULL pointer");
    ok (flux_buffer_read (NULL, -1, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_read fails on NULL pointer");
    ok (flux_buffer_write (NULL, NULL, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write fails on NULL pointer");
    ok (flux_buffer_lines (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_lines fails on NULL pointer");
    ok (flux_buffer_drop_line (NULL) < 0
        && errno == EINVAL,
        "flux_buffer_drop_line fails on NULL pointer");
    ok (flux_buffer_peek_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_peek_line fails on NULL pointer");
    ok (flux_buffer_read_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_read_line fails on NULL pointer");
    ok (flux_buffer_write_line (NULL, "foo") < 0
        && errno == EINVAL,
        "flux_buffer_write_line fails on NULL pointer");
    ok (flux_buffer_peek_to_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_peek_to_fd fails on NULL pointer");
    ok (flux_buffer_read_to_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_read_to_fd fails on NULL pointer");
    ok (flux_buffer_write_from_fd (NULL, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write_from_fd fails on NULL pointer");

    ok ((fb = flux_buffer_create (FLUX_BUFFER_TEST_MAXSIZE)) != NULL,
        "flux_buffer_create works");

    /* write corner case tests */

    ok (flux_buffer_write (fb, NULL, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write fails on bad input");
    ok (flux_buffer_write (fb, "foo", -1) < 0
        && errno == EINVAL,
        "flux_buffer_write fails on bad input");
    ok (flux_buffer_write_line (fb, NULL) < 0
        && errno == EINVAL,
        "flux_buffer_write_line fails on bad input");
    ok (flux_buffer_write_from_fd (fb, -1, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write_from_fd fails on bad input");

    /* flux_buffer_destroy works with NULL */
    flux_buffer_destroy (NULL);

    flux_buffer_destroy (fb);

    /* all functions fail on destroyed fb pointer */
    ok (flux_buffer_bytes (fb) < 0
        && errno == EINVAL,
        "flux_buffer_bytes fails on destroyed fb pointer");
    ok (flux_buffer_drop (fb, -1) < 0
        && errno == EINVAL,
        "flux_buffer_drop fails on destroyed fb pointer");
    ok (flux_buffer_peek (fb, -1, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_peek fails on destroyed fb pointer");
    ok (flux_buffer_read (fb, -1, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_read fails on destroyed fb pointer");
    ok (flux_buffer_write (fb, NULL, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write fails on destroyed fb pointer");
    ok (flux_buffer_lines (fb) < 0
        && errno == EINVAL,
        "flux_buffer_lines fails on destroyed fb pointer");
    ok (flux_buffer_drop_line (fb) < 0
        && errno == EINVAL,
        "flux_buffer_drop_line fails on destroyed fb pointer");
    ok (flux_buffer_peek_line (fb, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_peek_line fails on destroyed fb pointer");
    ok (flux_buffer_read_line (fb, NULL) == NULL
        && errno == EINVAL,
        "flux_buffer_read_line fails on destroyed fb pointer");
    ok (flux_buffer_write_line (fb, "foo") < 0
        && errno == EINVAL,
        "flux_buffer_write_line fails on destroyed fb pointer");
    ok (flux_buffer_peek_to_fd (fb, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_peek_to_fd fails destroyed fb pointer");
    ok (flux_buffer_read_to_fd (fb, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_read_to_fd fails destroyed fb pointer");
    ok (flux_buffer_write_from_fd (fb, 0, 0) < 0
        && errno == EINVAL,
        "flux_buffer_write_from_fd fails on destroyed fb pointer");
}

void full_buffer (void)
{
    flux_buffer_t *fb;

    ok ((fb = flux_buffer_create (4)) != NULL,
        "flux_buffer_create works");

    ok (flux_buffer_write (fb, "1234", 4) == 4,
        "flux_buffer_write success");

    ok (flux_buffer_write (fb, "5", 1) < 0
        && errno == ENOSPC,
        "flux_buffer_write fails with ENOSPC if exceeding buffer size");

    ok (flux_buffer_drop (fb, -1) == 4,
        "flux_buffer_drop works");

    ok (flux_buffer_write_line (fb, "1234") < 0
        && errno == ENOSPC,
        "flux_buffer_write_line fails with ENOSPC if exceeding buffer size");

    flux_buffer_destroy (fb);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    corner_case ();
    full_buffer ();

    done_testing();

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

