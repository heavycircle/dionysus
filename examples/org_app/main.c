#include <org/config.h>
#include <stdio.h>

// org/config.h comes from org_library, which this project never vendors or
// add_subdirectory()s: it's on DINO_LIBRARY_PATH (see CMakeLists.txt).

static const char *const service_config = "# billing service\n"
                                          "name    = billing\n"
                                          "port    = 8443\n"
                                          "region  = eu-west\n";

int main(void)
{
    org_config_t *config = org_config_parse(service_config);
    if (!config)
        return 1;

    const char *name    = org_config_get(config, "name", "unnamed");
    long        port    = org_config_get_long(config, "port", 80);
    const char *region  = org_config_get(config, "region", "us-east");
    long        workers = org_config_get_long(config, "workers", 4);
    printf("[Config] %d keys\n", org_config_count(config));
    printf("[Config] %s on port %ld in %s, %ld workers (default)\n", name, port, region, workers);

    int ok = port == 8443 && workers == 4;
    org_config_free(config);
    if (!ok)
        printf("[Config] unexpected values (FAILED)\n");
    return ok ? 0 : 1;
}
