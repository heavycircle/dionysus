#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parse_arguments(int argc, char **argv, AppOptions *opts)
{
    // Set defaults
    opts->limit = 50;
    opts->verbose = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
        {
            opts->verbose = 1;
        }
        else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--limit") == 0)
        {
            if (i + 1 < argc)
            {
                opts->limit = atoi(argv[i + 1]);
                i++; // Skip the next argument since we consumed it as the limit
            }
            else
            {
                printf("Error: --limit requires an integer argument.\n");
                return 0;
            }
        }
        else
        {
            printf("Unknown argument: %s\n", argv[i]);
            printf("Usage: %s [-v] [-l limit]\n", argv[0]);
            return 0;
        }
    }
    return 1;
}
