#include "main.h"

int main(void)
{
    char *one = "hello world!";
    char *two = "goodbye world!";

    if (strcmp("hello world", "goodbye world") == 0)
    {
        LOG_INFO("'%s' == '%s'", one, two);
    }
    else
    {
        LOG_INFO("'%s' != '%s'", one, two);
    }

    return 0;
}
