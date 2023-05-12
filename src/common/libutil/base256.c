/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
/* base256.c - a binary to emoji encoding
 *
 * Adapted from https://github.com/Equim-chan/libb256
 * Copyright (c) 2017, Equim
 * BSD 3-Clause License
 *
 * Specification:
 *
 * base256 encoding uses two tables to transform a single byte
 * into a single emoji. The tables can be found in table.go. In
 * this implementation, these tables are utilized circularly when
 * encoding.
 *
 * To achieve the best compatibility, all the emojis are picked
 * from the classic version and each of them is guaranteed to be
 * 4 bytes long in UTF-8.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "ccan/str/str.h"
#include "base256.h"

static const char* enc_tab[2][256] = {{
    "😀", "😁", "😂", "😃", "😄", "😅", "😆", "😉", "😊", "😋", "😎", "😍", "😘", "😗", "😙", "😚",
    "🙂", "🤗", "🤔", "😐", "😑", "😶", "🙄", "😏", "😣", "😥", "😮", "🤐", "😯", "😪", "😫", "😴",
    "😌", "🤓", "😛", "😜", "😝", "😒", "😓", "😔", "😕", "🙃", "🤑", "😲", "🙁", "😖", "😞", "😟",
    "😤", "😢", "😭", "😦", "😨", "😩", "😬", "😰", "😱", "😳", "😵", "😡", "😠", "😇", "😷", "🤒",
    "🤕", "😈", "👿", "👹", "👺", "💀", "👻", "👽", "👾", "🤖", "💩", "😺", "😸", "😹", "😻", "😼",
    "😽", "🙀", "😿", "😾", "🙈", "🙉", "🙊", "👦", "👧", "👨", "👩", "👴", "👵", "👶", "👼", "👮",
    "🕵", "💂", "👷", "👳", "👱", "🎅", "👸", "👰", "👲", "🙍", "🙎", "🙅", "🙆", "💁", "🙋", "🙇",
    "💆", "💇", "🚶", "🏃", "💃", "👯", "🕴", "🗣", "👤", "👥", "🏇", "🏂", "🏌", "🏄", "🚣", "🏊",
    "🏋", "🚴", "🚵", "🏎", "🏍", "👫", "👬", "👭", "💏", "💑", "👪", "💪", "👈", "👉", "👆", "🖕",
    "👇", "🖖", "🤘", "🖐", "👌", "👍", "👎", "👊", "👋", "👏", "👐", "🙌", "🙏", "💅", "👂", "👃",
    "👣", "👀", "👁", "👅", "👄", "💋", "💘", "💓", "💔", "💕", "💖", "💗", "💙", "💚", "💛", "💜",
    "💝", "💞", "💟", "💌", "💤", "💢", "💣", "💥", "💦", "💨", "💫", "💬", "🗨", "🗯", "💭", "🕳",
    "👓", "🕶", "👔", "👕", "👖", "👗", "👘", "👙", "👚", "👛", "👜", "👝", "🛍", "🎒", "👞", "👟",
    "👠", "👡", "👢", "👑", "👒", "🎩", "🎓", "📿", "💄", "💍", "💎", "🐵", "🐒", "🐶", "🐕", "🐩",
    "🐺", "🐱", "🐈", "🦁", "🐯", "🐅", "🐆", "🐴", "🐎", "🦄", "🐮", "🐂", "🐃", "🐄", "🐷", "🐖",
    "🐗", "🐽", "🐏", "🐑", "🐐", "🐪", "🐫", "🐘", "🐭", "🐁", "🐀", "🐹", "🐰", "🐇", "🐿", "🐻"
}, {
    "🐨", "🐼", "🐾", "🦃", "🐔", "🐓", "🐣", "🐤", "🐥", "🐦", "🐧", "🕊", "🐸", "🐊", "🐢", "🐍",
    "🐲", "🐉", "🐳", "🐋", "🐬", "🐟", "🐠", "🐡", "🐙", "🐚", "🦀", "🐌", "🐛", "🐜", "🐝", "🐞",
    "🕷", "🕸", "🦂", "💐", "🌸", "💮", "🏵", "🌹", "🌺", "🌻", "🌼", "🌷", "🌱", "🌲", "🌳", "🌴",
    "🌵", "🌾", "🌿", "🍀", "🍁", "🍂", "🍃", "🍇", "🍈", "🍉", "🍊", "🍋", "🍌", "🍍", "🍎", "🍏",
    "🍐", "🍑", "🍒", "🍓", "🍅", "🍆", "🌽", "🌶", "🍄", "🌰", "🍞", "🧀", "🍖", "🍗", "🍔", "🍟",
    "🍕", "🌭", "🌮", "🌯", "🍳", "🍲", "🍿", "🍱", "🍘", "🍙", "🍚", "🍛", "🍜", "🍝", "🍠", "🍢",
    "🍣", "🍤", "🍥", "🍡", "🍦", "🍧", "🍨", "🍩", "🍪", "🎂", "🍰", "🍫", "🍬", "🍭", "🍮", "🍯",
    "🍼", "🍵", "🍶", "🍾", "🍷", "🍸", "🍹", "🍺", "🍻", "🍽", "🍴", "🔪", "🏺", "🌍", "🌎", "🌏",
    "🌐", "🗺", "🗾", "🏔", "🌋", "🗻", "🏕", "🏖", "🏜", "🏝", "🏞", "🏟", "🏛", "🏗", "🏘", "🏙",
    "🏚", "🏠", "🏡", "🏢", "🏣", "🏤", "🏥", "🏦", "🏨", "🏩", "🏪", "🏫", "🏬", "🏭", "🏯", "🏰",
    "💒", "🗼", "🗽", "🕌", "🕍", "🕋", "🌁", "🌃", "🌄", "🌅", "🌆", "🌇", "🌉", "🌌", "🎠", "🎡",
    "🎢", "💈", "🎪", "🎭", "🖼", "🎨", "🎰", "🚂", "🚃", "🚄", "🚅", "🚆", "🚇", "🚈", "🚉", "🚊",
    "🚝", "🚞", "🚋", "🚌", "🚍", "🚎", "🚐", "🚑", "🚒", "🚓", "🚔", "🚕", "🚖", "🚗", "🚘", "🚙",
    "🚚", "🚛", "🚜", "🚲", "🚏", "🛣", "🛤", "🚨", "🚥", "🚦", "🚧", "🚤", "🛳", "🛥", "🚢", "🛩",
    "🛫", "🛬", "💺", "🚁", "🚟", "🚠", "🚡", "🚀", "🛰", "🛎", "🚪", "🛌", "🛏", "🛋", "🚽", "🚿",
    "🛀", "🛁", "🕰", "🌞", "🌝", "🌚", "🌑", "🌒", "🌓", "🌔", "🌕", "🌖", "🌗", "🌘", "🌜", "🌛"
}};

int base256_encode (char *buf,
                    int buflen,
                    void *data,
                    int datalen)
{
    register unsigned char table = 0;
    int i = 0;
    int n = 0;

    if (buf == NULL || data == NULL || buflen <= 0 || datalen < 0) {
        errno = EINVAL;
        return -1;
    }

    /* Copy prefix */
    memcpy (buf, BASE64_PREFIX, strlen(BASE64_PREFIX));
    n += strlen (BASE64_PREFIX);

    while (i < datalen && n < buflen - 2) {
        memcpy (buf + n, enc_tab[table][((unsigned char *) data)[i]], 4);

        table = 1 - table;
        i++;
        n += 4;
    }
    buf[n++] = '\0';
    return n;
}

static int base256_lookup (const char *c, char *result)
{
    for (int i = 0; i < 512; i++) {
        const char *index = enc_tab[i / 256][i % 256];
        if (memcmp (c, index, 4) == 0) {
            *result = (char) i;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

int base256_decode (void *buf, int buflen, const char *in)
{
    int in_size;
    const char *cur;
    int i = 0;
    int n = 0;

    if (buf == NULL || in == NULL || buflen <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (!strstarts (in, BASE64_PREFIX)) {
        errno = EINVAL;
        return -1;
    }

    in_size = strlen (in);

    /* Skip past prefix */
    i += strlen (BASE64_PREFIX);
    do {
        char c;
        cur = in + i;
        if (base256_lookup (cur, &c) < 0)
            return -1;
        ((char *)buf)[n] = c;
        i += 4;
        n++;
    } while (i < in_size && n < buflen);

    return n;
}
