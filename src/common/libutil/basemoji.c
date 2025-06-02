/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* basemoji.c - an emoji encoding for unsigned 64 bit integers
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "ccan/array_size/array_size.h"
#include "basemoji.h"

/* Minimum length of a b576 string is 1 emoji, or 4 bytes */
#define BASEMOJI_MINLEN 4

/* Maximum number of emoji "digits" in a basemoji string is
 *
 *  ceil (ln (2^64-1)/ln (576)) = 7
 *
 * 4 bytes per emoji, so 4*7 = 28 bytes.
 */
#define BASEMOJI_MAXLEN 28

/*  The following is a Selection of 576 emoji in CLDR[1] collation order[2]
 *  taken from the version 2010 Unicode emoji set[3]. Note: Selected code
 *  points are all represented in 4 bytes, which is assumed in the
 *  implementation in this module. Additionally, every character in this
 *  selected set has a common first two bytes of F0 9F in UTF-8 encoding,
 *  which aids in detection of a valid basemoji string.
 *
 *  1. https://cldr.unicode.org
 *  2. https://unicode.org/emoji/charts-12.1/emoji-ordering.txt
 *  3. https://unicode.org/emoji/charts/emoji-versions.html
 *
 */
const char *emojis[] = {
"😃", "😄", "😁", "😆", "😅", "😂", "😉", "😊", "😍", "😘", "😚", "😋",
"😜", "😝", "😏", "😒", "😌", "😔", "😪", "😷", "😵", "😲", "😳", "😨",
"😰", "😥", "😢", "😭", "😱", "😖", "😣", "😞", "😓", "😩", "😫", "😤",
"😡", "😠", "👿", "💀", "💩", "👹", "👺", "👻", "👽", "👾", "😺", "😸",
"😹", "😻", "😼", "😽", "🙀", "😿", "😾", "🙈", "🙉", "🙊", "💌", "💘",
"💝", "💖", "💗", "💓", "💞", "💕", "💟", "💔", "💛", "💚", "💙", "💜",
"💋", "💯", "💢", "💥", "💫", "💦", "💨", "💬", "💤", "👋", "👌", "👈",
"👉", "👆", "👇", "👍", "👎", "👊", "👏", "🙌", "👐", "🙏", "💅", "💪",
"👂", "👃", "👀", "👅", "👄", "👶", "👦", "👧", "👱", "👨", "👩", "👴",
"👵", "🙍", "🙎", "🙅", "🙆", "💁", "🙋", "🙇", "👮", "💂", "👷", "👸",
"👳", "👲", "👰", "👼", "🎅", "💆", "💇", "🚶", "🏃", "💃", "👯", "🏂",
"🏄", "🏊", "🛀", "👫", "💏", "💑", "👪", "👤", "👣", "🐵", "🐒", "🐶",
"🐩", "🐺", "🐱", "🐯", "🐴", "🐎", "🐮", "🐷", "🐗", "🐽", "🐑", "🐫",
"🐘", "🐭", "🐹", "🐰", "🐻", "🐨", "🐼", "🐾", "🐔", "🐣", "🐤", "🐥",
"🐦", "🐧", "🐸", "🐢", "🐍", "🐲", "🐳", "🐬", "🐟", "🐠", "🐡", "🐙",
"🐚", "🐌", "🐛", "🐜", "🐝", "🐞", "💐", "🌸", "💮", "🌹", "🌺", "🌻",
"🌼", "🌷", "🌱", "🌴", "🌵", "🌾", "🌿", "🍀", "🍁", "🍂", "🍃", "🍄",
"🍇", "🍈", "🍉", "🍊", "🍌", "🍍", "🍎", "🍏", "🍑", "🍒", "🍓", "🍅",
"🍆", "🌽", "🌰", "🍞", "🍖", "🍗", "🍔", "🍟", "🍕", "🍳", "🍲", "🍱",
"🍘", "🍙", "🍚", "🍛", "🍜", "🍝", "🍠", "🍢", "🍣", "🍤", "🍥", "🍡",
"🍦", "🍧", "🍨", "🍩", "🍪", "🎂", "🍰", "🍫", "🍬", "🍭", "🍮", "🍯",
"🍵", "🍶", "🍷", "🍸", "🍹", "🍺", "🍻", "🍴", "🔪", "🌏", "🗾", "🌋",
"🗻", "🏠", "🏡", "🏢", "🏣", "🏥", "🏦", "🏨", "🏩", "🏪", "🏫", "🏬",
"🏭", "🏯", "🏰", "💒", "🗼", "🗽", "🌁", "🌃", "🌄", "🌅", "🌆", "🌇",
"🌉", "🎠", "🎡", "🎢", "💈", "🎪", "🚃", "🚄", "🚅", "🚇", "🚉", "🚌",
"🚑", "🚒", "🚓", "🚕", "🚗", "🚙", "🚚", "🚲", "🚏", "🚨", "🚥", "🚧",
"🚤", "🚢", "💺", "🚀", "🕛", "🕐", "🕑", "🕒", "🕓", "🕔", "🕕", "🕖",
"🕗", "🕘", "🕙", "🕚", "🌑", "🌓", "🌔", "🌕", "🌙", "🌛", "🌟", "🌠",
"🌌", "🌀", "🌈", "🌂", "🔥", "💧", "🌊", "🎃", "🎄", "🎆", "🎇", "🎈",
"🎉", "🎊", "🎋", "🎍", "🎎", "🎏", "🎐", "🎑", "🎀", "🎁", "🎫", "🏆",
"🏀", "🏈", "🎾", "🎳", "🎣", "🎽", "🎿", "🎯", "🔫", "🎱", "🔮", "🎮",
"🎰", "🎲", "🃏", "🀄", "🎴", "🎭", "🎨", "👓", "👔", "👕", "👖", "👗",
"👘", "👙", "👚", "👛", "👜", "👝", "🎒", "👞", "👟", "👠", "👡", "👢",
"👑", "👒", "🎩", "🎓", "💄", "💍", "💎", "🔊", "📢", "📣", "🔔", "🎼",
"🎵", "🎶", "🎤", "🎧", "📻", "🎷", "🎸", "🎹", "🎺", "🎻", "📱", "📲",
"📞", "📟", "📠", "🔋", "🔌", "💻", "💽", "💾", "💿", "📀", "🎥", "🎬",
"📺", "📷", "📹", "📼", "🔍", "🔎", "💡", "🔦", "🏮", "📔", "📕", "📖",
"📗", "📘", "📙", "📚", "📓", "📒", "📃", "📜", "📄", "📰", "📑", "🔖",
"💰", "💴", "💵", "💸", "💳", "💹", "📧", "📨", "📩", "📤", "📥", "📦",
"📫", "📪", "📮", "📝", "💼", "📁", "📂", "📅", "📆", "📇", "📈", "📉",
"📊", "📋", "📌", "📍", "📎", "📏", "📐", "🔒", "🔓", "🔏", "🔐", "🔑",
"🔨", "💣", "🔧", "🔩", "🔗", "📡", "💉", "💊", "🚪", "🚽", "🚬", "🗿",
"🏧", "🚹", "🚺", "🚻", "🚼", "🚾", "🚫", "🚭", "🔞", "🔃", "🔙", "🔚",
"🔛", "🔜", "🔝", "🔯", "🔼", "🔽", "🎦", "📶", "📳", "📴", "💱", "💲",
"🔱", "📛", "🔰", "🔟", "🔠", "🔡", "🔢", "🔣", "🔤", "🆎", "🆑", "🆒",
"🆓", "🆔", "🆕", "🆖", "🆗", "🆘", "🆙", "🆚", "🈁", "🈶", "🈯", "🉐",
"🈹", "🈚", "🈲", "🉑", "🈸", "🈴", "🈳", "🈺", "🈵", "🔴", "🔵", "🔶",
"🔷", "🔸", "🔹", "🔺", "🔻", "💠", "🔘", "🔳", "🔲", "🏁", "🚩", "🎌",
};

bool is_basemoji_string (const char *s)
{
    int len = strlen (s);

    /* This code assumes length of emoji array is 576
     * Generate error at build time if this becomes untrue:
     */
    BUILD_ASSERT(ARRAY_SIZE(emojis) == 576);

    /* Check for expected length of a basemoji string, and if the
     * first two bytes match the expected UTF-8 encoding.
     * This doesn't guarantee that `s` is a valid basemoji string,
     * but this will catch most obvious cases and other invalid strings
     * are left to be detected in decode.
     */
    if (len >= BASEMOJI_MINLEN
        && len <= BASEMOJI_MAXLEN
        && len % 4 == 0
        && (uint8_t)s[0] == 0xf0
        && (uint8_t)s[1] == 0x9f)
        return true;
    return false;
}

/* Encode id into buf in reverse (i.e. higher order bytes are encoded
 * and placed first into 'buf' since we're doing progressive division.)
 */
static int emoji_revenc (char *buf, int buflen, uint64_t id)
{
    int index = 0;
    memset (buf, 0, buflen);
    if (id == 0) {
        memcpy (buf, emojis[0], 4);
        return 4;
    }
    while (id > 0) {
        int rem = id % 576;
        memcpy (buf+index, emojis[rem], 4);
        index += 4;
        id = id / 576;
    }
    return index;
}

int uint64_basemoji_encode (uint64_t id, char *buf, int buflen)
{
    int count;
    int n;
    char reverse[BASEMOJI_MAXLEN+1];

    if (buf == NULL || buflen <= 0) {
        errno = EINVAL;
        return -1;
    }

    /* Encode bytes to emoji (in reverse), which also gives us a count
     * of the total bytes required for this encoding.
     */
    if ((count = emoji_revenc (reverse, sizeof (reverse), id)) < 0) {
        errno = EINVAL;
        return -1;
    }

    /*  Check for overflow of provided buffer:
     *  Need space for count bytes for emoji + NUL
     */
    if (count + 1 > buflen) {
        errno = EOVERFLOW;
        return -1;
    }

    memset (buf, 0, buflen);
    n = 0;

    /* Copy 4-byte emojis back in order so that most significant bits are
     * on the left:
     */
    for (int i = 0; i < count; i+=4) {
        memcpy (buf+n, reverse+(count-4-i), 4);
        n+=4;
    }
    return 0;
}


static int basemoji_lookup (const char *c, int *result)
{
    for (int i = 0; i < 576; i++) {
        if (memcmp (c, emojis[i], 4) == 0) {
            *result = i;
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

int uint64_basemoji_decode (const char *str, uint64_t *idp)
{
    uint64_t id = 0;
    uint64_t scale = 1;
    int len;

    if (str == NULL
        || idp == NULL
        || !is_basemoji_string (str)) {
        errno = EINVAL;
        return -1;
    }

    /* Move through basemoji string in reverse since least significant
     * bits are at the end. Since all emoji are 4 bytes, start at 4 from
     * the end to point to the final emoji.
     */
    len = strlen (str);
    for (int i = len - 4; i >=  0; i-=4) {
        int c;
        if (basemoji_lookup (str+i, &c) < 0) {
            errno = EINVAL;
            return -1;
        }
        id += c * scale;
        scale *= 576;
    }
    *idp = id;
    return 0;
}
