#ifndef SERVER_H
#define SERVER_H

#include <dino/core/network.h>

#include "kv.h"

/**
 * Reads one newline-terminated request from sock, executes it against db and
 * sends the response line back.
 *
 * @return 0 when a request was served, 1 when the peer closed the connection,
 *         -1 on a socket error.
 */
int kv_serve_one(struct kv *db, dino_socket_t sock);

#endif // SERVER_H
