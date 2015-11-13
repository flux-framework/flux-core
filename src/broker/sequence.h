/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/
#ifndef BROKER_SEQUENCE_H
#define BROKER_SEQUENCE_H

#include <flux/core.h>
#include "src/common/libutil/shortjson.h"

typedef struct seq_struct seqhash_t;

/* Create/destroy sequence generator handle
 */
seqhash_t *sequence_hash_create (void);
void sequence_hash_destroy (seqhash_t *seq);

int sequence_request_handler (seqhash_t *seq, const flux_msg_t *msg, JSON *op);

#endif /* BROKER_SEQUENCE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
