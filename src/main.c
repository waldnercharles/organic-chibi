#include "driver.h"

int
main(int argc, char **argv)
{
    return driver_run(stdout, stderr, argc, argv);
}
