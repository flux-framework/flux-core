#ifndef _BROKER_PEER_H
#define _BROKER_PEER_H

typedef struct {
    int lastseen;       /* epoch peer was last heard from */
    bool modflag;       /* true if this peer is a comms module */
    bool mute;          /* stop CC'ing events over this connection */
} peer_t;

peer_t *peer_create (void);
peer_t *peer_add (zhash_t *zh, const char *uuid);
peer_t *peer_lookup (zhash_t *zh, const char *uuid);

int peer_get_lastseen (peer_t *p);
void peer_set_lastseen (peer_t *p, int epoch);

bool peer_get_modflag (peer_t *p);
void peer_set_modflag (peer_t *p, bool val);

bool peer_get_mute (peer_t *p);
void peer_set_mute (peer_t *p, bool val);

/* Lookup peer by 'uuid', creating it if not found.
 * Then call peer_set_lastseen (now).
 */
void peer_checkin (zhash_t *zh, const char *uuid, int now);

/* Lookup peer by 'uuid'.
 * If not found return 'now', else 'now' - peer_get_lastseen().
 */
int peer_idle (zhash_t *zh, const char *uuid, int now);

/* Lookup peer by 'uuid', creating it if not found.
 * Then call peer_set_mute().
 */
void peer_mute (zhash_t *zh, const char *uuid);

#endif /* !_BROKER_PEER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
