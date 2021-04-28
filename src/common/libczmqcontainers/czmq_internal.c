/*  =========================================================================
    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of CZMQ, the high-level C binding for 0MQ:
    http://czmq.zeromq.org.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#include "czmq_containers.h"
#include "czmq_internal.h"

//  --------------------------------------------------------------------------
//  Free a provided string, and nullify the parent pointer. Safe to call on
//  a null pointer.

void
zstr_free (char **string_p)
{
    assert (string_p);
    free (*string_p);
    *string_p = NULL;
}
