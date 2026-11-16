#ifndef KV_H
#define KV_H

#include <stddef.h>

/** An in-memory string-to-string store. */
struct kv;

/** @return A new, empty store, or NULL if out of memory. */
struct kv *kv_create(void);

/** Frees the store and every key and value in it. */
void kv_destroy(struct kv *db);

/**
 * Sets key to value, replacing any previous value. Both are copied.
 *
 * @return 0 on success, -1 if out of memory.
 */
int kv_set(struct kv *db, const char *key, const char *value);

/** @return The value for key, or NULL if it isn't set. Valid until the next change. */
const char *kv_get(const struct kv *db, const char *key);

/** @return 0 if key was removed, -1 if it wasn't set. */
int kv_del(struct kv *db, const char *key);

/** @return The number of keys. */
size_t kv_count(const struct kv *db);

#endif // KV_H
