#ifndef CLI_H
#define CLI_H

typedef struct
{
    int limit;
    int verbose;
} AppOptions;

// Returns 1 on success, 0 on failure
int parse_arguments(int argc, char **argv, AppOptions *opts);

#endif
