#include <org/config.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <dino/util/logging.h>

struct entry
{
    char *key;
    char *value;
};

struct org_config
{
    char         *text; // every key and value points into this copy
    struct entry *entries;
    int           count;
};

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
    return s;
}

static struct entry *find(const org_config_t *config, const char *key)
{
    for (int i = 0; i < config->count; i++)
    {
        if (strcmp(config->entries[i].key, key) == 0)
            return &config->entries[i];
    }
    return NULL;
}

static int add_line(org_config_t *config, char *line, int number)
{
    char *comment = strchr(line, '#');
    if (comment)
        *comment = '\0';
    line = trim(line);
    if (*line == '\0')
        return 0;

    char *eq = strchr(line, '=');
    if (!eq || eq == line)
    {
        DINO_LOG_WARN("config line %d: expected `key = value`, skipped", number);
        return 0;
    }
    *eq         = '\0';
    char *key   = trim(line);
    char *value = trim(eq + 1);

    // A repeated key takes its last value.
    struct entry *existing = find(config, key);
    if (existing)
    {
        existing->value = value;
        return 0;
    }
    struct entry *grown = realloc(config->entries, (config->count + 1) * sizeof(*grown));
    if (!grown)
        return -1;
    config->entries                  = grown;
    config->entries[config->count++] = (struct entry) { key, value };
    return 0;
}

org_config_t *org_config_parse(const char *text)
{
    org_config_t *config = calloc(1, sizeof(*config));
    if (!config)
        return NULL;
    config->text = malloc(strlen(text) + 1);
    if (!config->text)
    {
        free(config);
        return NULL;
    }
    strcpy(config->text, text);

    int   number = 1;
    char *line   = config->text;
    while (line)
    {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        if (add_line(config, line, number++) != 0)
        {
            org_config_free(config);
            return NULL;
        }
        line = next;
    }
    return config;
}

const char *org_config_get(const org_config_t *config, const char *key, const char *fallback)
{
    const struct entry *e = find(config, key);
    return e ? e->value : fallback;
}

long org_config_get_long(const org_config_t *config, const char *key, long fallback)
{
    const char *value = org_config_get(config, key, NULL);
    if (!value || *value == '\0')
        return fallback;
    char *end;
    errno       = 0;
    long number = strtol(value, &end, 10);
    return *end != '\0' || errno == ERANGE ? fallback : number;
}

int org_config_count(const org_config_t *config)
{
    return config->count;
}

void org_config_free(org_config_t *config)
{
    if (!config)
        return;
    free(config->entries);
    free(config->text);
    free(config);
}
