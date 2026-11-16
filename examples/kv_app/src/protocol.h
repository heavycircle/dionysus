#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#include "kv.h"

/** Longest request or response line, including the terminating NUL. */
#define KV_LINE_MAX 256

/**
 * Executes one request against db and writes the response into out.
 *
 *   SET <key> <value>   -> OK              (the value may contain spaces)
 *   GET <key>           -> VALUE <value>   or NOT_FOUND
 *   DEL <key>           -> OK              or NOT_FOUND
 *   COUNT               -> COUNT <n>
 *
 * @param  line     Request without the trailing newline.
 * @param  out      Response buffer, at least KV_LINE_MAX bytes.
 * @return 0 if the request was understood, -1 if not (out holds "ERR ...").
 */
int kv_execute(struct kv *db, const char *line, char *out, size_t out_len);

#endif // PROTOCOL_H
