#ifndef _BROKER_PEER_H
#define _BROKER_PEER_H

/* The peerhash contains entries for modules and overlay peers (1 hop),
 * hashed by uuid.
 */

typedef struct {
    int lastseen;
    bool modflag;
    bool mute;
} peer_t;

typedef struct {
    zhash_t *zh;
    heartbeat_t *hb;
} peerhash_t;

peerhash_t *peerhash_create (void);
void peerhash_destroy (peerhash_t *ph);

peer_t *peer_add (peerhash_t *ph, const char *uuid);
peer_t *peer_lookup (peerhash_t *ph, const char *uuid);

/* Get a list of uuid's represented in the hash.
 * Caller must free with zlist_destroy().
 */
zlist_t *peerhash_keys (peerhash_t *ph);

/* Give 'ph' a reference to heartbeat.
 * This allows peer_idle() and peer_checkin() to query the
 * heartbeat class for the current epoch.
 */
void peerhash_set_heartbeat (peerhash_t *ph, heartbeat_t *hb);

bool peer_get_modflag (peer_t *p);
void peer_set_modflag (peer_t *p, bool val);

bool peer_get_mute (peer_t *p);
void peer_set_mute (peer_t *p, bool val);

/* Lookup peer by 'uuid', creating it if not found.
 * Then call peer_set_lastseen (now).
 */
void peer_checkin (peerhash_t *ph, const char *uuid);

/* Lookup peer by 'uuid'.
 * If not found return 'now', else 'now' - peer_get_lastseen().
 */
int peer_idle (peerhash_t *ph, const char *uuid);

/* Lookup peer by 'uuid', creating it if not found.
 * Then call peer_set_mute().
 */
void peer_mute (peerhash_t *ph, const char *uuid);

/* Create a JSON object that can be used to form the response
 * to an 'lspeer' query (E.g. from "flux-comms idle").
 * Module peers are excluded - only overlay peers are returned.
 */
json_object *peer_list_encode (peerhash_t *ph);

#endif /* !_BROKER_PEER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
