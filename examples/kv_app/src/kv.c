#include "kv.h"

#include <stdlib.h>
#include <string.h>

struct entry
{
    char *key;
    char *value;
};

// A plain array: a sample app needs clarity, not O(1) lookups.
struct kv
{
    struct entry *entries;
    size_t        count;
    size_t        capacity;
};

static char *copy(const char *s)
{
    size_t len = strlen(s) + 1;
    char  *out = malloc(len);
    if (out)
        memcpy(out, s, len);
    return out;
}

static struct entry *find(const struct kv *db, const char *key)
{
    for (size_t i = 0; i < db->count; i++)
    {
        if (strcmp(db->entries[i].key, key) == 0)
            return &db->entries[i];
    }
    return NULL;
}

struct kv *kv_create(void)
{
    return calloc(1, sizeof(struct kv));
}

void kv_destroy(struct kv *db)
{
    if (!db)
        return;
    for (size_t i = 0; i < db->count; i++)
    {
        free(db->entries[i].key);
        free(db->entries[i].value);
    }
    free(db->entries);
    free(db);
}

int kv_set(struct kv *db, const char *key, const char *value)
{
    char *value_copy = copy(value);
    if (!value_copy)
        return -1;

    struct entry *existing = find(db, key);
    if (existing)
    {
        free(existing->value);
        existing->value = value_copy;
        return 0;
    }

    if (db->count == db->capacity)
    {
        size_t        capacity = db->capacity ? db->capacity * 2 : 8;
        struct entry *grown    = realloc(db->entries, capacity * sizeof(struct entry));
        if (!grown)
        {
            free(value_copy);
            return -1;
        }
        db->entries  = grown;
        db->capacity = capacity;
    }

    char *key_copy = copy(key);
    if (!key_copy)
    {
        free(value_copy);
        return -1;
    }
    db->entries[db->count++] = (struct entry) { key_copy, value_copy };
    return 0;
}

const char *kv_get(const struct kv *db, const char *key)
{
    const struct entry *e = find(db, key);
    return e ? e->value : NULL;
}

int kv_del(struct kv *db, const char *key)
{
    struct entry *e = find(db, key);
    if (!e)
        return -1;
    free(e->key);
    free(e->value);
    // Order doesn't matter, so the last entry fills the gap.
    *e = db->entries[--db->count];
    return 0;
}

size_t kv_count(const struct kv *db)
{
    return db->count;
}
