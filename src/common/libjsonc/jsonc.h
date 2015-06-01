#ifndef _FLUX_CORE_JSONC_H
#define _FLUX_CORE_JSONC_H

#define flux_json_request           jsonc_request
#define flux_json_respond           jsonc_respond
#define flux_err_respond            jsonc_respond_err
#define flux_json_request_decode    jsonc_request_decode
#define flux_json_response_decode   jsonc_response_decode

#define flux_json_rpc               jsonc_rpc
#define flux_json_multrpc           jsonc_multrpc

#include "request.h"
#include "rpc.h"

#endif /* !_FLUX_CORE_JSONC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
