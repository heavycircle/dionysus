#include "protocol.h"

#include <stdio.h>
#include <string.h>

// Splits off the next space-separated word. Returns NULL when none is left.
static char *next_word(char **cursor)
{
    char *s = *cursor;
    while (*s == ' ')
        s++;
    if (*s == '\0')
        return NULL;
    char *end = strchr(s, ' ');
    if (end)
    {
        *end    = '\0';
        *cursor = end + 1;
    }
    else
    {
        *cursor = s + strlen(s);
    }
    return s;
}

static int reply(char *out, size_t out_len, int rc, const char *fmt, const char *arg)
{
    snprintf(out, out_len, fmt, arg);
    return rc;
}

int kv_execute(struct kv *db, const char *line, char *out, size_t out_len)
{
    char buf[KV_LINE_MAX];
    if (strlen(line) >= sizeof(buf))
        return reply(out, out_len, -1, "ERR %s", "request too long");
    strcpy(buf, line);

    char *cursor  = buf;
    char *command = next_word(&cursor);
    if (!command)
        return reply(out, out_len, -1, "ERR %s", "empty request");

    if (strcmp(command, "SET") == 0)
    {
        char *key = next_word(&cursor);
        while (*cursor == ' ')
            cursor++;
        if (!key || *cursor == '\0')
            return reply(out, out_len, -1, "ERR %s", "usage: SET <key> <value>");
        if (kv_set(db, key, cursor) != 0)
            return reply(out, out_len, -1, "ERR %s", "out of memory");
        return reply(out, out_len, 0, "%s", "OK");
    }
    if (strcmp(command, "GET") == 0 || strcmp(command, "DEL") == 0)
    {
        char *key = next_word(&cursor);
        if (!key || next_word(&cursor))
            return reply(out, out_len, -1, "ERR usage: %s <key>", command);
        if (command[0] == 'G')
        {
            const char *value = kv_get(db, key);
            return value ? reply(out, out_len, 0, "VALUE %s", value)
                         : reply(out, out_len, 0, "%s", "NOT_FOUND");
        }
        return reply(out, out_len, 0, "%s", kv_del(db, key) == 0 ? "OK" : "NOT_FOUND");
    }
    if (strcmp(command, "COUNT") == 0)
    {
        if (next_word(&cursor))
            return reply(out, out_len, -1, "ERR %s", "usage: COUNT");
        snprintf(out, out_len, "COUNT %zu", kv_count(db));
        return 0;
    }
    return reply(out, out_len, -1, "ERR unknown command: %s", command);
}
