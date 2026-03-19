#include "logic.h"

#include <stdio.h>

// Static means this helper function is private to this file
static int is_prime(int n)
{
    if (n <= 1)
        return 0;
    for (int i = 2; i * i <= n; i++)
    {
        if (n % i == 0)
            return 0;
    }
    return 1;
}

void calculate_primes(int limit, int verbose)
{
    int count = 0;
    for (int i = 2; i <= limit; i++)
    {
        if (is_prime(i))
        {
            count++;
            if (verbose)
            {
                printf("Found prime: %d\n", i);
            }
        }
    }
    printf("Success! Found %d prime numbers up to %d.\n", count, limit);
}
