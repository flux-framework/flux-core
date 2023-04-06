/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_PMI_EXCHANGE_H
#define SHELL_PMI_EXCHANGE_H

/* Create handle for performing multiple sequential exchanges.
 * 'k' is the tree fanout (k=0 selects internal default).
 */
struct pmi_exchange *pmi_exchange_create (flux_shell_t *shell, int k);
void pmi_exchange_destroy (struct pmi_exchange *pex);

typedef void (*pmi_exchange_f)(struct pmi_exchange *pex, void *arg);

/* Perform one exchange across all shell ranks.
 * 'dict' is the input from this  shell.  Once the the result of the exchange
 * is available, 'cb' is invoked.
 */
int pmi_exchange (struct pmi_exchange *pex,
                  json_t *dict,
                  pmi_exchange_f cb,
                  void *arg);

/* Accessors may be called only from pmi_exchange_f callback.
 * pmi_exchange_get_dict() returns a json object that is invalidated when
 * the callback returns.
 */
bool pmi_exchange_has_error (struct pmi_exchange *pex);
json_t *pmi_exchange_get_dict (struct pmi_exchange *pex);

#endif /* !SHELL_PMI_EXCHANGE_H */

/* vi: ts=4 sw=4 expandtab
 */

