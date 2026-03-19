#include "cli.h"
#include "logic.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    AppOptions opts;

    // 1. Parse terminal arguments
    if (!parse_arguments(argc, argv, &opts))
    {
        return 1; // Exit with error code if parsing failed
    }

    // 2. Run the application
    if (opts.verbose)
    {
        printf("Starting computation...\n");
    }

    calculate_primes(opts.limit, opts.verbose);

    return 0;
}
