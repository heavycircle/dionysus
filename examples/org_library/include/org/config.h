#ifndef ORG_CONFIG_H
#define ORG_CONFIG_H

// The organization's configuration format: one `key = value` per line, `#`
// starts a comment, blank lines are ignored. Malformed lines are skipped with
// a warning through dino's logger, so every project reports them the same way.

typedef struct org_config org_config_t;

// Returns NULL only when out of memory.
org_config_t *org_config_parse(const char *text);

// The value for `key`, or `fallback` if it isn't set. Valid until
// org_config_free.
const char *org_config_get(const org_config_t *config, const char *key, const char *fallback);

// The value for `key` as a decimal integer, or `fallback` if it isn't set or
// isn't a number.
long org_config_get_long(const org_config_t *config, const char *key, long fallback);

// How many keys are set.
int org_config_count(const org_config_t *config);

void org_config_free(org_config_t *config);

#endif
