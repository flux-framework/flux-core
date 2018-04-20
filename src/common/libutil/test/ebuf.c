#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "src/common/libutil/ebuf.h"
#include "src/common/libtap/tap.h"

#define EBUF_TEST_MAXSIZE 1048576

void empty_cb (ebuf_t *eb, void *arg)
{
    /* do nothing */
}

void basic (void)
{
    ebuf_t *eb;
    const char *ptr;
    int len;

    ok ((eb = ebuf_create (EBUF_TEST_MAXSIZE)) != NULL,
        "ebuf_create works");

    ok (ebuf_bytes (eb) == 0,
        "ebuf_bytes initially returns 0");

    /* write & peek tests */

    ok (ebuf_write (eb, "foo", 3) == 3,
        "ebuf_write works");

    ok (ebuf_bytes (eb) == 3,
        "ebuf_bytes returns length of bytes written");

    ok ((ptr = ebuf_peek (eb, 2, &len)) != NULL
        && len == 2,
        "ebuf_peek with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "ebuf_peek returns exepected data");

    ok ((ptr = ebuf_peek (eb, -1, &len)) != NULL
        && len == 3,
        "ebuf_peek with length -1 works");

    ok (!memcmp (ptr, "foo", 3),
        "ebuf_peek returns exepected data");

    ok (ebuf_bytes (eb) == 3,
        "ebuf_bytes returns unchanged length after peek");

    ok (ebuf_drop (eb, 2) == 2,
        "ebuf_drop works");

    ok (ebuf_bytes (eb) == 1,
        "ebuf_bytes returns length of remaining bytes written");

    ok (ebuf_drop (eb, -1) == 1,
        "ebuf_drop drops remaining bytes");

    ok (ebuf_bytes (eb) == 0,
        "ebuf_bytes returns 0 with all bytes dropped");

    /* write and read tests */

    ok (ebuf_write (eb, "foo", 3) == 3,
        "ebuf_write works");

    ok (ebuf_bytes (eb) == 3,
        "ebuf_bytes returns length of bytes written");

    ok ((ptr = ebuf_read (eb, 2, &len)) != NULL
        && len == 2,
        "ebuf_read with specific length works");

    ok (!memcmp (ptr, "fo", 2),
        "ebuf_read returns exepected data");

    ok (ebuf_bytes (eb) == 1,
        "ebuf_bytes returns new length after read");

    ok ((ptr = ebuf_read (eb, -1, &len)) != NULL
        && len == 1,
        "ebuf_peek with length -1 works");

    ok (!memcmp (ptr, "o", 1),
        "ebuf_peek returns exepected data");

    ok (ebuf_bytes (eb) == 0,
        "ebuf_bytes returns 0 with all bytes read");

    /* write_line & peek_line tests */

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 on no line");

    ok (ebuf_write_line (eb, "foo") == 4,
        "ebuf_write_line works");

    ok (ebuf_bytes (eb) == 4,
        "ebuf_bytes returns length of bytes written");

    ok (ebuf_line (eb) == 1,
        "ebuf_line returns 1 on line written");

    ok ((ptr = ebuf_peek_line (eb, &len)) != NULL
        && len == 4,
        "ebuf_peek_line works");

    ok (!memcmp (ptr, "foo\n", 4),
        "ebuf_peek_line returns exepected data");

    ok (ebuf_bytes (eb) == 4,
        "ebuf_bytes returns unchanged length after peek_line");

    ok (ebuf_drop_line (eb) == 4,
        "ebuf_drop_line works");

    ok (ebuf_bytes (eb) == 0,
        "ebuf_bytes returns 0 after drop_line");

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 after drop_line");

    /* write_line & read_line tests */

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 on no line");

    ok (ebuf_write_line (eb, "foo") == 4,
        "ebuf_write_line works");

    ok (ebuf_bytes (eb) == 4,
        "ebuf_bytes returns length of bytes written");

    ok (ebuf_line (eb) == 1,
        "ebuf_line returns 1 on line written");

    ok ((ptr = ebuf_read_line (eb, &len)) != NULL
        && len == 4,
        "ebuf_peek_line works");

    ok (!memcmp (ptr, "foo\n", 4),
        "ebuf_peek_line returns exepected data");

    ok (ebuf_bytes (eb) == 0,
        "ebuf_bytes returns 0 after read_line");

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 after read_line");

    ebuf_destroy (eb);
}

void read_cb (ebuf_t *eb, void *arg)
{
    int *count = arg;
    const char *ptr;
    int len;

    (*count)++;

    ok ((ptr = ebuf_read (eb, -1, &len)) != NULL
        && len == 6,
        "ebuf_read in callback works");

    ok (!memcmp (ptr, "foobar", 6),
        "read in callback returns expected data");
}

void read_line_cb (ebuf_t *eb, void *arg)
{
    int *count = arg;
    const char *ptr;
    int len;

    (*count)++;

    ok ((ptr = ebuf_read (eb, -1, &len)) != NULL
        && len == 7,
        "ebuf_read in callback works");

    ok (!memcmp (ptr, "foobar\n", 7),
        "read in callback returns expected data");
}

void write_cb (ebuf_t *eb, void *arg)
{
    int *count = arg;

    (*count)++;

    ok (ebuf_write (eb, "a", 1) == 1,
        "ebuf_write in callback works");
}

void basic_callback (void)
{
    ebuf_t *eb;
    const char *ptr;
    int len;
    int count;

    ok ((eb = ebuf_create (EBUF_TEST_MAXSIZE)) != NULL,
        "ebuf_create works");

    /* low read callback */

    count = 0;
    ok (ebuf_set_low_read_cb (eb, read_cb, 3, &count) == 0,
        "ebuf_set_low_read_cb success");

    ok (ebuf_write (eb, "foobar", 6) == 6,
        "ebuf_write success");

    ok (count == 1,
        "read_cb called");

    ok (ebuf_bytes (eb) == 0,
        "ebuf_bytes returns 0 because callback read all data");

    ok (ebuf_write (eb, "foo", 3) == 3,
        "ebuf_write success");

    ok (count == 1,
        "read_cb not called again, because not above low mark");

    count = 0;
    ok (ebuf_set_low_read_cb (eb, NULL, 0, &count) == 0,
        "ebuf_set_low_read_cb clear callback success");

    ok (ebuf_write (eb, "foo", 3) == 3,
        "ebuf_write success");

    ok (count == 0,
        "read_cb cleared successfully");

    ok (ebuf_drop (eb, -1) == 6,
        "ebuf_drop cleared all data");

    /* read line callback */

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 on no line");

    count = 0;
    ok (ebuf_set_read_line_cb (eb, read_line_cb, &count) == 0,
        "ebuf_set_low_read_cb success");

    ok (ebuf_write (eb, "foo", 3) == 3,
        "ebuf_write success");

    ok (count == 0,
        "read_line_cb not called, no line written yet");

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 on no line");

    ok (ebuf_write (eb, "bar\n", 4) == 4,
        "ebuf_write success");

    ok (ebuf_bytes (eb) == 0,
        "ebuf_bytes returns 0 because callback read all data");

    ok (count == 1,
        "read_line_cb called");

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 on no line, callback read all data");

    count = 0;
    ok (ebuf_set_read_line_cb (eb, NULL, &count) == 0,
        "ebuf_set_low_read_cb clear callback success");

    ok (ebuf_write_line (eb, "foo") == 4,
        "ebuf_write_line success");

    ok (count == 0,
        "read_line_cb cleared successfully");

    ok (ebuf_line (eb) == 1,
        "ebuf_line returns 1, callback did not read line");

    ok (ebuf_drop (eb, -1) == 4,
        "ebuf_drop cleared all data");

    ok (ebuf_line (eb) == 0,
        "ebuf_line returns 0 after drop line");

    /* high write callback w/ read */

    ok (ebuf_write (eb, "foobar", 6) == 6,
        "ebuf_write success");

    count = 0;
    ok (ebuf_set_high_write_cb (eb, write_cb, 3, &count) == 0,
        "ebuf_set_high_write_cb success");

    ok ((ptr = ebuf_read (eb, 3, &len)) != NULL
        && len == 3,
        "ebuf_read success");

    ok (!memcmp (ptr, "foo", 3),
        "ebuf_read returns expected data");

    ok (count == 0,
        "write_cb not called, not less than high");

    ok ((ptr = ebuf_read (eb, 3, &len)) != NULL
        && len == 3,
        "ebuf_read success");

    ok (!memcmp (ptr, "bar", 3),
        "ebuf_read returns expected data");

    ok (count == 1,
        "write_cb called");

    ok (ebuf_bytes (eb) == 1,
        "ebuf_bytes returns 1 because callback wrote a byte");

    count = 0;
    ok (ebuf_set_high_write_cb (eb, NULL, 0, &count) == 0,
        "ebuf_set_high_write_cb clear callback success");

    ok ((ptr = ebuf_read (eb, -1, &len)) != NULL
        && len == 1,
        "ebuf_read success");

    ok (!memcmp (ptr, "a", 1),
        "ebuf_read returns expected data");

    ok (count == 0,
        "write_cb cleared successfully");

    /* high write callback w/ drop */

    ok (ebuf_write (eb, "foobar", 6) == 6,
        "ebuf_write success");

    count = 0;
    ok (ebuf_set_high_write_cb (eb, write_cb, 3, &count) == 0,
        "ebuf_set_high_write_cb success");

    ok (ebuf_drop (eb, 3) == 3,
        "ebuf_drop success");

    ok (count == 0,
        "write_cb not called, not less than high");

    ok (ebuf_drop (eb, 1) == 1,
        "ebuf_drop success");

    ok (count == 1,
        "write_cb called");

    ok (ebuf_bytes (eb) == 3,
        "ebuf_bytes return correct bytes after drop and write cb");

    count = 0;
    ok (ebuf_set_high_write_cb (eb, NULL, 0, &count) == 0,
        "ebuf_set_high_write_cb clear callback success");

    ok (ebuf_drop (eb, 1) == 1,
        "ebuf_drop success");

    ok (count == 0,
        "write_cb cleared successfully");

    ok (ebuf_drop (eb, -1) == 2,
        "ebuf_drop success");

    ebuf_destroy (eb);
}

void corner_case (void)
{
    ebuf_t *eb;

    ok (ebuf_create (-1) == NULL
        && errno == EINVAL,
        "ebuf_create fails on bad input -1");

    ok (ebuf_create (0) == NULL
        && errno == EINVAL,
        "ebuf_create fails on bad input 0");

    /* all functions fail on NULL eb pointer */
    ok (ebuf_bytes (NULL) < 0
        && errno == EINVAL,
        "ebuf_bytes fails on NULL pointer");
    ok (ebuf_set_low_read_cb (NULL, empty_cb, 0, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_low_read_cb fails on NULL pointer");
    ok (ebuf_set_read_line_cb (NULL, empty_cb, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_read_line_cb fails on NULL pointer");
    ok (ebuf_set_high_write_cb (NULL, empty_cb, 0, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_high_write_cb fails on NULL pointer");
    ok (ebuf_drop (NULL, -1) < 0
        && errno == EINVAL,
        "ebuf_drop fails on NULL pointer");
    ok (ebuf_peek (NULL, -1, NULL) == NULL
        && errno == EINVAL,
        "ebuf_peek fails on NULL pointer");
    ok (ebuf_read (NULL, -1, NULL) == NULL
        && errno == EINVAL,
        "ebuf_read fails on NULL pointer");
    ok (ebuf_line (NULL) < 0
        && errno == EINVAL,
        "ebuf_line fails on NULL pointer");
    ok (ebuf_drop_line (NULL) < 0
        && errno == EINVAL,
        "ebuf_drop_line fails on NULL pointer");
    ok (ebuf_peek_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "ebuf_peek_line fails on NULL pointer");
    ok (ebuf_read_line (NULL, NULL) == NULL
        && errno == EINVAL,
        "ebuf_read_line fails on NULL pointer");
    ok (ebuf_write (NULL, NULL, 0) < 0
        && errno == EINVAL,
        "ebuf_write fails on NULL pointer");
    ok (ebuf_write_line (NULL, "foo") < 0
        && errno == EINVAL,
        "ebuf_write_line fails on NULL pointer");

    ok ((eb = ebuf_create (EBUF_TEST_MAXSIZE)) != NULL,
        "ebuf_create works");

    /* callback corner case tests */

    ok (ebuf_set_low_read_cb (eb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_low_read_cb fails on bad input");
    ok (ebuf_set_low_read_cb (eb, empty_cb, 0, NULL) == 0,
        "ebuf_set_low_read_cb success");
    ok (ebuf_set_low_read_cb (eb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_low_read_cb fails on bad input overwrite callback");
    ok (ebuf_set_read_line_cb (eb, empty_cb, NULL) < 0
        && errno == EEXIST,
        "ebuf_set_read_line_cb fails if callback already set");
    ok (ebuf_set_high_write_cb (eb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "ebuf_set_high_write_cb fails if callback already set");
    ok (ebuf_set_low_read_cb (eb, NULL, 0, NULL) == 0,
        "ebuf_set_low_read_cb success clear callback");

    ok (ebuf_set_read_line_cb (eb, empty_cb, NULL) == 0,
        "ebuf_set_read_line_cb success");
    ok (ebuf_set_low_read_cb (eb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "ebuf_set_low_read_cb fails if callback already set");
    ok (ebuf_set_high_write_cb (eb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "ebuf_set_high_write_cb fails if callback already set");
    ok (ebuf_set_read_line_cb (eb, NULL, NULL) == 0,
        "ebuf_set_read_line_cb success clear callback");

    ok (ebuf_set_high_write_cb (eb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_high_write_cb fails on bad input");
    ok (ebuf_set_high_write_cb (eb, empty_cb, 0, NULL) == 0,
        "ebuf_set_high_write_cb success");
    ok (ebuf_set_high_write_cb (eb, empty_cb, -1, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_high_write_cb fails on bad input overwrite callback");
    ok (ebuf_set_low_read_cb (eb, empty_cb, 0, NULL) < 0
        && errno == EEXIST,
        "ebuf_set_low_read_cb fails if callback already set");
    ok (ebuf_set_read_line_cb (eb, empty_cb, NULL) < 0
        && errno == EEXIST,
        "ebuf_set_read_line_cb fails if callback already set");
    ok (ebuf_set_high_write_cb (eb, NULL, 0, NULL) == 0,
        "ebuf_set_high_write_cb success clear callback");

    /* write corner case tests */

    ok (ebuf_write (eb, NULL, 0) < 0
        && errno == EINVAL,
        "ebuf_write fails on bad input");
    ok (ebuf_write (eb, "foo", -1) < 0
        && errno == EINVAL,
        "ebuf_write fails on bad input");
    ok (ebuf_write_line (eb, NULL) < 0
        && errno == EINVAL,
        "ebuf_write_line fails on bad input");

    /* ebuf_destroy works with NULL */
    ebuf_destroy (NULL);

    ebuf_destroy (eb);

    /* all functions fail on destroyed eb pointer */
    ok (ebuf_bytes (eb) < 0
        && errno == EINVAL,
        "ebuf_bytes fails on destroyed eb pointer");
    ok (ebuf_set_low_read_cb (eb, empty_cb, 0, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_low_read_cb fails on destroyed eb pointer");
    ok (ebuf_set_read_line_cb (eb, empty_cb, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_read_line_cb fails on destroyed eb pointer");
    ok (ebuf_set_high_write_cb (eb, empty_cb, 0, NULL) < 0
        && errno == EINVAL,
        "ebuf_set_high_write_cb fails on destroyed eb pointer");
    ok (ebuf_drop (eb, -1) < 0
        && errno == EINVAL,
        "ebuf_drop fails on destroyed eb pointer");
    ok (ebuf_peek (eb, -1, NULL) == NULL
        && errno == EINVAL,
        "ebuf_peek fails on destroyed eb pointer");
    ok (ebuf_read (eb, -1, NULL) == NULL
        && errno == EINVAL,
        "ebuf_read fails on destroyed eb pointer");
    ok (ebuf_line (eb) < 0
        && errno == EINVAL,
        "ebuf_line fails on destroyed eb pointer");
    ok (ebuf_drop_line (eb) < 0
        && errno == EINVAL,
        "ebuf_drop_line fails on destroyed eb pointer");
    ok (ebuf_peek_line (eb, NULL) == NULL
        && errno == EINVAL,
        "ebuf_peek_line fails on destroyed eb pointer");
    ok (ebuf_read_line (eb, NULL) == NULL
        && errno == EINVAL,
        "ebuf_read_line fails on destroyed eb pointer");
    ok (ebuf_write (eb, NULL, 0) < 0
        && errno == EINVAL,
        "ebuf_write fails on destroyed eb pointer");
    ok (ebuf_write_line (eb, "foo") < 0
        && errno == EINVAL,
        "ebuf_write_line fails on destroyed eb pointer");
}

void full_buffer (void)
{
    ebuf_t *eb;

    ok ((eb = ebuf_create (4)) != NULL,
        "ebuf_create works");

    ok (ebuf_write (eb, "1234", 4) == 4,
        "ebuf_write success");

    ok (ebuf_write (eb, "5", 1) < 0
        && errno == ENOSPC,
        "ebuf_write fails with ENOSPC if exceeding buffer size");

    ok (ebuf_drop (eb, -1) == 4,
        "ebuf_drop works");

    ok (ebuf_write_line (eb, "1234") < 0
        && errno == ENOSPC,
        "ebuf_write_line fails with ENOSPC if exceeding buffer size");

    ebuf_destroy (eb);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    basic ();
    basic_callback ();
    corner_case ();
    full_buffer ();

    done_testing();

    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

