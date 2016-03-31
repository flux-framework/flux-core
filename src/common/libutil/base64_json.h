#ifndef _UTIL_BASE64_JSON_H
#define _UTIL_BASE64_JSON_H

/* N.B. for get_data(): caller must free returned data if non-NULL.
 */
json_object *base64_json_encode (uint8_t *dat, int len);

int base64_json_decode (json_object *o, uint8_t **datp, int *lenp);

#endif /* !UTIL_BASE64_JSON_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
